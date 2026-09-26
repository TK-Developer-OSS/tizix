/*
 * tizix.c - tzcc の生成アセンブリを Tizix 用「IY 相対 位置独立コード(PIC)」へ変換する。
 *
 * Tizix プロセスは IY = ロード先ベースアドレス。全ラベルは 0 基準オフセット。
 * 動的アドレスが生じる箇所で実行時 IY を加算する。
 *   ../tizix/user/iy_reg_claude.py の検証済みシーケンスを移植したもの。
 *   ただし tzcc は SDCC と違いグローバル変数を _DATA に置き ld (nn),a で触るので、
 *   その絶対データアクセスも IY 相対へ書き換える(SDCC-Tizix には無いケース)。
 *
 * ==== IX ベース方式 (tizix #31) ====
 *   tzcc は値をレジスタに保持せず、全ローカルを _DATA の var_* に置く。素朴に
 *   IY 加算すると 1 アクセスあたり
 *       ld hl,#var_X / push af / push de / push iy / pop de / add hl,de /
 *       pop de / pop af / <(hl) で読み書き>            = 15〜17B
 *   を払う。これが coreutils 全体の 36% を占めていた(tizix #31 実測)。
 *
 *   そこで **IX = IY + tzc_vb + 128** を張り、スカラ変数を (ix+d) で触る:
 *       ld l, d(ix) / ld h, d+1(ix)          6B  (旧 15B)
 *       ld d(ix), l / ld d+1(ix), h          6B  (旧 17B)
 *       ld a, d(ix) / ld d(ix), a            3B  (旧 14〜16B)
 *   ±127 の窓に収めるため、_DATA を並べ替えてスカラ(.dw/.db 単発)を先頭に集め、
 *   tzc_vb からの数値オフセットをこのパスが自分で決める。配列(.ds)・文字列
 *   (.ascii)・long(4B)はスカラ扱いしない = 従来どおり IY 加算のグルーで触る。
 *
 *   IX の張り直しは **関数入口と call の直後だけ**。呼び先(tzcc 関数も
 *   libtzc の手書き asm も カーネルベクタも)が IX をフレームポインタとして
 *   潰すので、戻ったら張り直す必要がある。逆にそれさえやれば関数内のどの地点
 *   でも IX は有効なので、ラベル(合流点)で無効化する必要はない。
 *     ラベルで無効化して「触る直前に遅延で張る」方式も試したが、生成コードは
 *     ラベルが密(wc で 50 個)なため張り直しが増えて逆に損だった
 *     (wc 3126 -> 2806B。eager 方式は 2454B)。加えて `call f / jr L` のように
 *     call 後に無効なまま合流点へ飛ぶ経路があり、遅延方式は正しくない。
 *
 *   関数プロローグの `ld ix,#0 / add ix,sp`(仮引数読み出し用フレーム)は IX を
 *   奪うので撤去し、仮引数は SP 相対(HL 経由)で読む。SP は触っていないので
 *   generator が計算したオフセットはそのまま使える。
 *
 * 触るもの:
 *   ld hl, #var_X / #str_N / #Lxxx   → 直後に HL += IY
 *   ld hl, (var_X)                   → (ix+d) ないし IY 加算 + 16bit ロード
 *   ld a,  (var_X)                   → (ix+d) ないし IY 加算 + ld a,(hl)
 *   ld (var_X), hl / a               → (ix+d) ないし アドレス計算して (hl) へ
 *   ld <r>, N(ix)                    → SP 相対の仮引数読み出しへ
 *   call _name                       → IY 加算して ___sdcc_call_hl 経由
 *   jp Lxxx / jp z,Lxxx              → jr へ(範囲外は後段 jrfix.py が間接化)
 *   .area _DATA                      → スカラを先頭に集めて並べ替え
 * 触らないもの:
 *   jr / djnz / ret / jp (hl) / 数値即値 / .db/.ascii/.ds/.dw / ラベル定義
 *   ld hl,(_name)  (std ストリーム。Tizix では未対応。素通し)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 変換できない再配置参照を見つけたら立てる。素通しすると 0 基準の絶対番地の
 * まま実行されゼロページへ wild jump し、**実行時にしか分からずフレークする**
 * (tizix memory: flaky-wildjump-root-cause)。必ずビルドを止める。 */
static int g_xform_error = 0;

/* ---------------- スカラ変数表 (IX 窓) ---------------- */
#define MAXVAR   1024
#define NAMELEN  96
#define IXBIAS   128            /* IX = IY + tzc_vb + IXBIAS。d は -128..+127 */
#define NODISP   (-9999)

static char g_vname[MAXVAR][NAMELEN];
static int  g_voff[MAXVAR];     /* tzc_vb からのバイトオフセット */
static int  g_vsz[MAXVAR];      /* 1 or 2 */
static int  g_nvar = 0;

/* この翻訳単位に (ix+d) で触れるスカラが 1 個でもあるか。無いなら IX を
 * 張る意味が無いので入口/call 後の張り直しごと省く(id/uname/whoami 等)。 */
static int  g_use_ix = 0;

/* name のスカラが (ix+d) で届くなら d を、届かない/スカラでないなら
 * NODISP を返す。need は必要バイト数(1 or 2)。 */
static int var_disp(const char *name, int need) {
    int i;
    for (i = 0; i < g_nvar; i++) {
        if (strcmp(g_vname[i], name) == 0) {
            int d = g_voff[i] - IXBIAS;
            if (g_vsz[i] < need) return NODISP;
            if (d < -128 || d + need - 1 > 127) return NODISP;
            return d;
        }
    }
    return NODISP;
}

/* operand が「IY 加算すべきセグメント内ラベル」か。数値/文字定数/外部固定シンボルは false。 */
static int is_reloc(const char *op) {
    while (*op == ' ' || *op == '\t' || *op == '#' || *op == '(') op++;
    if (*op == '-' || *op == '+' || isdigit((unsigned char)*op)) return 0;
    if (*op == '\'' || *op == '"') return 0;
    if (strncmp(op, "___sdcc_call_hl", 15) == 0) return 0;
    if (strncmp(op, "_kexit", 6) == 0) return 0;
    if (strncmp(op, "_kputchar", 9) == 0) return 0;
    if (strncmp(op, "_kgetchar", 9) == 0) return 0;
    if (strncmp(op, "_getticks", 9) == 0) return 0;
    if (isalpha((unsigned char)*op) || *op == '_' || *op == '.') return 1;
    return 0;
}

/* "    ld hl, (_stdin)" 等 std ストリーム? */
static int is_std_ref(const char *s) {
    return strstr(s, "(_stdin)") || strstr(s, "(_stdout)") || strstr(s, "(_stderr)");
}

static void emit_iy_hl(FILE *o) {
    fputs("    push af\n    push de\n    push iy\n    pop de\n    add hl, de\n    pop de\n    pop af\n", o);
}

/* IX = IY + tzc_vb + IXBIAS を張る。
 *   keep_de: 直前が call のときは戻り値が DE:HL(long)の可能性があるので DE を
 *            保存する(11B)。関数入口では DE は死んでいるので保存しない(9B)。
 *   AF はどちらの地点でも死んでいる(call 直後のフラグに意味は無く、入口も同様)。 */
static void emit_ix_base(FILE *o, int keep_de) {
    if (!g_use_ix) return;
    if (keep_de) fputs("    push de\n", o);
    fputs("    ld ix, #tzc_vb+128\n    push iy\n    pop de\n    add ix, de\n", o);
    if (keep_de) fputs("    pop de\n", o);
}

/* 行頭の空白を飛ばす */
static const char *skipws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* "name:" 形式のラベル定義なら 1 */
static int is_label_def(const char *p) {
    const char *c = strchr(p, ':');
    const char *sp = strpbrk(p, " \t");
    return c && (!sp || c < sp);
}

/* "ld <r>, N(ix)" にマッチしたら r を *reg、N を *off に入れて 1 を返す。 */
static int match_ix_read(const char *p, char *reg, int *off) {
    if (strncmp(p, "ld ", 3) != 0) return 0;
    p = skipws(p + 3);
    if (!*p || p[1] != ',') return 0;
    *reg = *p;
    if (!strchr("lhaed", *reg)) return 0;
    p = skipws(p + 2);
    if (*p != '-' && !isdigit((unsigned char)*p)) return 0;
    *off = atoi(p);
    while (*p == '-' || isdigit((unsigned char)*p)) p++;
    return strncmp(p, "(ix)", 4) == 0;
}

/* SP 相対の 16bit 読み出しを HL へ。 */
static void emit_sp_read16(FILE *o, int off) {
    fprintf(o, "    ld hl, #%d\n    add hl, sp\n"
               "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n", off);
}

/* ---------------- _DATA の並べ替え ---------------- */

typedef struct {
    int  start, end;    /* 行インデックス(ラベル行 .. 最終ディレクティブ行) */
    int  scalar;
    char name[NAMELEN];
} DataItem;

static DataItem g_item[MAXVAR * 2];
static int g_nitem = 0;

/* lines[dstart..n-1] を走査して DataItem を作り、スカラにオフセットを振る。 */
static void collect_data(char **lines, int n, int dstart) {
    int i = dstart + 1;
    g_nitem = 0;
    g_nvar = 0;
    while (i < n) {
        const char *p = skipws(lines[i]);
        DataItem *it;
        const char *c;
        int len, j, ndir, kind;
        if (*p == '\0' || *p == ';') { i++; continue; }
        if (!is_label_def(p)) { i++; continue; }

        it = &g_item[g_nitem];
        c = strchr(p, ':');
        len = (int)(c - p);
        if (len > NAMELEN - 1) len = NAMELEN - 1;
        memcpy(it->name, p, len);
        it->name[len] = '\0';
        it->start = i;

        j = i + 1; ndir = 0; kind = 0;      /* kind: 1=.dw 2=.db 0=other */
        while (j < n) {
            const char *q = skipws(lines[j]);
            if (*q == '\0' || *q == ';') { j++; continue; }
            if (is_label_def(q)) break;
            if (*q != '.') break;
            if (ndir == 0) {
                if (strncmp(q, ".dw", 3) == 0) kind = 1;
                else if (strncmp(q, ".db", 3) == 0) kind = 2;
            }
            ndir++;
            j++;
        }
        it->end = j - 1;
        it->scalar = (ndir == 1 && kind != 0 && strncmp(it->name, "var_", 4) == 0);
        if (it->scalar && g_nvar < MAXVAR) {
            strcpy(g_vname[g_nvar], it->name + 4);
            g_vsz[g_nvar] = (kind == 1) ? 2 : 1;
            g_nvar++;
        } else {
            it->scalar = 0;
        }
        i = j;
        g_nitem++;
        if (g_nitem >= MAXVAR * 2) break;
    }
    /* オフセット割り当て(宣言順)。 */
    {
        int k, off = 0;
        for (k = 0; k < g_nvar; k++) {
            g_voff[k] = off;
            off += g_vsz[k];
        }
    }
}

/* ---------------- 行変換 ---------------- */

/* 行 line(改行なし) を変換して o へ書く。 */
static void xform_line(const char *line, FILE *o) {
    const char *p = skipws(line);

    /* --- ディレクティブ / 空行はそのまま --- */
    if (*p == '\0' || *p == ';' || *p == '.') { fprintf(o, "%s\n", line); return; }

    /* --- ラベル定義。関数の入口(_name::)なら IX を張る --- */
    if (is_label_def(p)) {
        fprintf(o, "%s\n", line);
        if (line[0] == '_' && strstr(p, "::")) emit_ix_base(o, 0);
        return;
    }

    /* --- ld hl, (var_X) --- */
    if (strncmp(p, "ld hl, (var_", 12) == 0) {
        char name[NAMELEN]; const char *q = p + 12; int i = 0; int d;
        while (*q && *q != ')' && i < NAMELEN - 1) name[i++] = *q++;
        name[i] = '\0';
        d = var_disp(name, 2);
        if (d != NODISP) {

            fprintf(o, "    ld l, %d(ix)\n    ld h, %d(ix)\n", d, d + 1);
            return;
        }
        fprintf(o, "    ld hl, #var_%s\n", name);
        emit_iy_hl(o);
        fputs("    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n", o);
        return;
    }
    /* --- ld a, (var_X) --- */
    if (strncmp(p, "ld a, (var_", 11) == 0) {
        char name[NAMELEN]; const char *q = p + 11; int i = 0; int d;
        while (*q && *q != ')' && i < NAMELEN - 1) name[i++] = *q++;
        name[i] = '\0';
        d = var_disp(name, 1);
        if (d != NODISP) {

            fprintf(o, "    ld a, %d(ix)\n", d);
            return;
        }
        fprintf(o, "    ld hl, #var_%s\n", name);
        emit_iy_hl(o);
        fputs("    ld a, (hl)\n", o);
        return;
    }
    /* --- ld (var_X), hl / a --- */
    if (strncmp(p, "ld (var_", 8) == 0) {
        const char *q = p + 8; char name[NAMELEN]; int i = 0;
        const char *rhs; int is_hl, d;
        while (*q && *q != ')' && i < NAMELEN - 1) name[i++] = *q++;
        name[i] = '\0';
        rhs = strstr(p, "), ");
        is_hl = rhs && rhs[3] == 'h';
        d = var_disp(name, is_hl ? 2 : 1);
        if (d != NODISP) {

            if (is_hl) fprintf(o, "    ld %d(ix), l\n    ld %d(ix), h\n", d, d + 1);
            else       fprintf(o, "    ld %d(ix), a\n", d);
            return;
        }
        fputs("    push af\n    push bc\n", o);
        if (is_hl) fputs("    ex de, hl\n", o);            /* de = 値 */
        else       fputs("    ld e, a\n", o);              /* e = 値(下位) */
        fprintf(o, "    ld hl, #var_%s\n", name);
        fputs("    push iy\n    pop bc\n    add hl, bc\n", o);
        if (is_hl) fputs("    ld (hl), e\n    inc hl\n    ld (hl), d\n", o);
        else       fputs("    ld (hl), e\n", o);
        fputs("    pop bc\n    pop af\n", o);
        return;
    }

    /* --- ld hl, #<reloc>  → 直後に HL += IY --- */
    if (strncmp(p, "ld hl, #", 8) == 0) {
        if (is_reloc(p + 8)) { fprintf(o, "%s\n", line); emit_iy_hl(o); return; }
        fprintf(o, "%s\n", line); return;
    }
    /* ld de,#reloc / ld bc,#reloc は tzcc は出さないが保険 */
    if (strncmp(p, "ld de, #", 8) == 0 && is_reloc(p + 8)) {
        fprintf(o, "%s\n", line);
        fputs("    push af\n    push hl\n    push iy\n    pop hl\n    add hl, de\n    ex de, hl\n    pop hl\n    pop af\n", o);
        return;
    }

    /* --- ld hl, (_stdX)  素通し(Tizix 未対応) --- */
    if (is_std_ref(p)) { fprintf(o, "%s\n", line); return; }

    /* --- call _name  → 間接 CALL --- */
    if (strncmp(p, "call ", 5) == 0) {
        const char *tgt = skipws(p + 5);
        if (is_reloc(tgt)) {
            char t[NAMELEN]; int i = 0;
            while (tgt[i] && tgt[i] != ' ' && tgt[i] != '\t' && i < NAMELEN - 1) { t[i] = tgt[i]; i++; }
            t[i] = '\0';
            fputs("    push af\n    push de\n", o);
            fprintf(o, "    ld hl, #%s\n", t);
            fputs("    push iy\n    pop de\n    add hl, de\n    pop de\n    pop af\n    call ___sdcc_call_hl\n", o);
            emit_ix_base(o, 1);              /* 呼び先が IX を潰すので張り直す */
            return;
        }
        fprintf(o, "%s\n", line);            /* 固定ベクタ call はそのまま */
        emit_ix_base(o, 1);
        return;
    }

    /* --- jp [cc,] LBL --------------------------------------------------
     * jr / djnz は相対分岐なのでロード先 base に依存せず、変換不要。よって
     * セグメント内 jp は **まず jr へ落とす**(3B → 2B、グルー 0B)。範囲外に
     * なった jr は後段の jrfix.py が、その分岐だけを間接列へ戻す。
     *   cf. tizix user/iy_reg_claude.py + iy_jrfix.py(同方針、実績あり)
     */
    if (strncmp(p, "jp ", 3) == 0 && p[3] != '(') {
        const char *q = skipws(p + 3);
        char cc[8]; int ci = 0;
        {
            const char *c = q;
            int n = 0;
            while (c[n] && c[n] != ',' && c[n] != ' ' && n < 7) n++;
            if (c[n] == ',') {                       /* 条件付き */
                for (ci = 0; ci < n; ci++) cc[ci] = c[ci];
                cc[n] = '\0';
                q = skipws(c + n + 1);
            } else {
                cc[0] = '\0';                        /* 無条件 */
            }
        }
        if (is_reloc(q)) {
            char t[NAMELEN]; int i = 0; int jrable;
            while (q[i] && q[i] != ' ' && q[i] != '\t' && i < NAMELEN - 1) { t[i] = q[i]; i++; }
            t[i] = '\0';
            jrable = (cc[0] == '\0')
                  || strcmp(cc, "z") == 0  || strcmp(cc, "nz") == 0
                  || strcmp(cc, "c") == 0  || strcmp(cc, "nc") == 0;
            if (jrable) {
                if (cc[0]) fprintf(o, "    jr %s, %s\n", cc, t);
                else       fprintf(o, "    jr %s\n", t);
                return;
            }
            /* jr で表せない条件(po/pe/p/m)。素通しすると 0 基準の絶対番地へ
             * 飛んでゼロページへ wild jump し、実行時にしか分からずフレークする。
             * tzcc は出さないので、ここへ来たら黙って通さず必ず落とす。 */
            fprintf(stderr, "tzcc --tizix-user: cannot relocate 'jp %s, %s' "
                            "(only z/nz/c/nc can become jr)\n", cc, t);
            g_xform_error = 1;
            fprintf(o, "%s\n", line);
            return;
        }
        fprintf(o, "%s\n", line); return;   /* 固定番地への jp はそのまま */
    }

    /* それ以外(jr/djnz/ret/ld 各種/算術/…)はそのまま */
    fprintf(o, "%s\n", line);
}

/* path のアセンブリを in-place で IY 相対へ変換。成功 0 / 失敗 1。 */
int tizix_iy_transform(const char *path) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t n;
    int nline = 0, cap = 4096;
    char **lines;
    int dstart = -1;
    FILE *o;

    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    buf = malloc(sz + 2);
    if (!buf) { fclose(f); return 1; }
    n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);

    lines = malloc(sizeof(char *) * cap);
    if (!lines) { free(buf); return 1; }
    {
        char *s = buf;
        while (*s) {
            char *nl = strchr(s, '\n');
            size_t ll;
            if (nl) *nl = '\0';
            ll = strlen(s);
            if (ll && s[ll - 1] == '\r') s[ll - 1] = '\0';
            if (nline == cap) {
                char **t;
                cap *= 2;
                t = realloc(lines, sizeof(char *) * cap);
                if (!t) { free(lines); free(buf); return 1; }
                lines = t;
            }
            lines[nline++] = s;
            if (!nl) break;
            s = nl + 1;
        }
    }

    {
        int i;
        for (i = 0; i < nline; i++) {
            const char *p = skipws(lines[i]);
            if (strncmp(p, ".area _DATA", 11) == 0) { dstart = i; break; }
        }
    }
    if (dstart >= 0) collect_data(lines, nline, dstart);
    /* 窓に届くスカラが無いなら IX を張らない(入口/call 後の 9〜11B が丸ごと不要)。 */
    g_use_ix = (g_nvar > 0);

    o = fopen(path, "wb");
    if (!o) { free(lines); free(buf); return 1; }
    fputs("; tzcc --tizix-user : IY 相対 PIC へ変換済み (IX = IY + tzc_vb + 128)\n", o);

    g_xform_error = 0;


    /* ---- コード部 ---- */
    {
        int i = 0;
        int end = (dstart >= 0) ? dstart : nline;
        while (i < end) {
            const char *p = skipws(lines[i]);
            char r0, r1, r2, r3;
            int o0, o1, o2, o3;

            /* 関数プロローグ `ld ix,#0 / add ix,sp` は撤去(IX は変数ベース用)。*/
            if (strncmp(p, "ld ix, #0", 9) == 0 && i + 1 < end) {
                const char *q = skipws(lines[i + 1]);
                if (strncmp(q, "add ix, sp", 10) == 0) { i += 2; continue; }
            }

            /* 仮引数の (ix) 読み出しを SP 相対へ。 */
            if (match_ix_read(p, &r0, &o0)) {
                if (r0 == 'l' && i + 1 < end &&
                    match_ix_read(skipws(lines[i + 1]), &r1, &o1) && r1 == 'h') {
                    if (i + 3 < end &&
                        match_ix_read(skipws(lines[i + 2]), &r2, &o2) && r2 == 'e' &&
                        match_ix_read(skipws(lines[i + 3]), &r3, &o3) && r3 == 'd') {
                        /* long 仮引数: 上位(DE)を先に読み、次に下位(HL) */
                        fprintf(o, "    ld hl, #%d\n    add hl, sp\n"
                                   "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n", o2);
                        emit_sp_read16(o, o0);
                        i += 4;
                        continue;
                    }
                    emit_sp_read16(o, o0);
                    i += 2;
                    continue;
                }
                if (r0 == 'a') {
                    fprintf(o, "    ld hl, #%d\n    add hl, sp\n    ld a, (hl)\n", o0);
                    i += 1;
                    continue;
                }
            }

            xform_line(lines[i], o);
            i++;
        }
    }

    /* ---- データ部: スカラを先頭へ集める ---- */
    if (dstart >= 0) {
        int k, j;
        fputs("\n    .area _DATA\n", o);
        fputs("tzc_vb:\n", o);
        for (k = 0; k < g_nitem; k++) {
            if (!g_item[k].scalar) continue;
            for (j = g_item[k].start; j <= g_item[k].end; j++)
                fprintf(o, "%s\n", lines[j]);
        }
        for (k = 0; k < g_nitem; k++) {
            if (g_item[k].scalar) continue;
            for (j = g_item[k].start; j <= g_item[k].end; j++)
                fprintf(o, "%s\n", lines[j]);
        }
    }

    fclose(o);
    free(lines);
    free(buf);
    return g_xform_error;
}
