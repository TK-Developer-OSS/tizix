/* user/vi.c - 外部コマンド vi(簡易版スクリーンエディタ)
 *
 *   vi FILE : FILE を編集する。無ければ新規扱い。
 *
 *   #33。~/vitest の簡易 vi を **設計だけ引き継いだ移植**。
 *   vitest 版のソースをそのまま持ってくることはできない ── 理由は 3 つ:
 *     (1) tizix の 1 プロセスは連続 4KB×最大 4 ブロック = 16KB。vitest 版は
 *         C ソースだけで 46KB あり、tzcc の生成コードでは全く入らない。
 *     (2) tzcc は struct のメンバが全て 2 バイト固定で char 配列メンバを持てず、
 *         Buffer 構造体をそのまま表現できない。sprintf も無い。
 *     (3) vitest 版はスワップファイルへ 1 行ずつ書き戻す設計で、編集のたびに
 *         FAT へ書く。cpmsim の FDC では 1 キー当たりのコストが重すぎる。
 *   そこで **本文まるごと RAM の平坦バッファ + バイト位置カーソル** という
 *   tizix 向けの構造に置き換えた。行配列も行インデックスも持たない
 *   (行頭・行末はその都度 '\n' を前後に走査して求める)。tzcc が苦手な
 *   多次元配列・関数ポインタ・struct を一切使わない形になっている。
 *
 *   掟(#31 / [[external-cmd-authoring-constraints]]):
 *     ・**関数間で同名ローカル禁止**(tzcc はスコープを持たず記憶域を共有する)。
 *       この 1 ファイル内で全ローカルに関数ごとの接頭辞を付けてある。
 *     ・sizeof(配列) は 2 を返すので使わない。定数で書く。
 *     ・除算・剰余を使わない。printf を使わない(prs / prnum / putchar)。
 *     ・条件式の中で代入しない。
 *     ・文字列リテラルのポインタ変数への代入は避ける(メッセージは msgid の
 *       整数コードにして render 側で literal を出す)。
 *
 *   端末は 24 行 x 80 桁固定(tizix に窓サイズを問い合わせる手段が無い)。
 *   1..23 行目が本文、24 行目がステータス。
 */
#include "stdio.h"

/* 本文バッファは **像に持たず、追加ブロックを丸ごと使う** (#38)。
 *   .BIN 先頭ヘッダに「追加ブロック 1 個」を刻んでおき(make tizixcmd XBLK=1)、
 *   getxbase() / getxsize() でその 4KB を受け取る。
 *   像に char text[N] を置くと、実データの無いゼロで .BIN がそのぶん太り、
 *   ロードも遅く、4KB 単位の切り上げで端数が無駄になる。
 *   おかげで **編集できるファイルが 1KB → 4KB** になった。
 *   上限は実行時に決まるので tmax(変数)で持つ。
 *   そのうち上端 1536B はスタック + argv、さらに 272B は undo の記録(#39)なので
 *   実効な本文は 2288B。 */
#define YANK_MAX 96         /* dd / yy が持てる 1 行の上限(実寸に合わせて縮小)*/
#define VI_STACK_RESERVE 1536  /* 追加ブロックの上端に残すスタック + argv 用の余白 */
#define NAME_MAX 40        /* 8.3 + パスに十分。実寸に詰めてある */
#define TROWS    23         /* 本文の表示行数 */
#define TCOLS    79         /* 1 行に出す最大桁数 */

/* getkey() が返す拡張キー。
 * 256 以上ではなく 0x81.. を使う: getchar() は 0..255 しか返さず、端末から
 * 7bit を超える生バイトは来ないので衝突しない。**switch の比較幅が 8bit に
 * 落ちても壊れない**ようにという保険でもある。 */
#define K_UP    0x81
#define K_DOWN  0x82
#define K_RIGHT 0x83
#define K_LEFT  0x84
/* K_DEL(Delete キー)だけは外してある。x(コマンド)/ BS(挿入)で足りる。
 */

/* ★型の幅は「読みやすさ」ではなく実費で決める。tzcc のコストモデルでは
 *   int/ポインタの読み書きが 1 回 6B、**char は 3B**。フラグ類を int で持つと
 *   その差が全アクセスに乗る(mode は毎打鍵で 4〜5 回読まれる)。
 *   0..255 に収まるものは全部 unsigned char にしてある。
 *   16bit が要るのは本文の位置(tmax/tlen/cur/top、最大 2560)と
 *   yank の長さだけ。 */
static char *text;                  /* 本文 = 追加ブロック(getxbase)。像に持たない */
static int  tmax;                   /* その大きさ(getxsize)。旧 TEXT_MAX */
static int  tlen;                   /* 本文のバイト数。末尾に改行は持たない */
static int  cur;                    /* カーソル位置(0..tlen)*/
static int  top;                    /* 画面最上行の行頭バイト位置 */
static unsigned char mode;          /* 0=コマンド 1=挿入 */
static unsigned char dirty;         /* 変更あり */
static unsigned char quitf;         /* 1 でメインループを抜ける */
static unsigned char toobig;        /* ファイルが tmax を超えた = 書き込み禁止 */
static unsigned char touched;       /* この 1 打鍵で画面を作り直す必要があるか */
/* 変更が「いまの 1 行の中だけ」なら、その行だけ書き直すための印。
 *   0 = 本文を触っていない / 1 = 行内だけ / 2 = 全面が要る。
 *   全 23 行を ESC[K で消して書き直すのがチラつきの正体で、
 *   文字を打つ / 消すのはほとんどが「行内だけ」だった。 */
static unsigned char lnr;
/* 最下行にいま出ている内容の印(dirty / mode / msgid だけで決まる)。
 *   同じなら 24 行目を触らない。毎打鍵 ESC[K で消して書き直すと
 *   カーソル移動だけでも最下行が点滅する = 「動きがチラつく」の正体。
 *   0xFF = 次は必ず書く(起動時・`:` の後・Ctrl+L)。 */
static unsigned char stold;
static unsigned char msgid;         /* ステータス行に出すメッセージ番号 */
static unsigned char scr_row;       /* mvcur が使う 1-based 画面座標 */
static unsigned char scr_col;

static char fname[NAME_MAX];
static char yank[YANK_MAX];
static unsigned char ylen;  /* yank の長さ。**0xFF = 未ヤンク**。
                             * かつて int で -1 を番兵にしていた頃、何もヤンクせずに
                             * p を押すと 1 文字が改行に化けていた。**tzcc では番兵を
                             * 0xFF 側に取り `==` で見る**(比較は 0 以外の定数だと
                             * 符号なし。#42 も参照)。 */
static unsigned char count; /* カウント接頭辞(3dd の 3)。0=無し。200 で頭打ち */

/* ファイルハンドルは 1 個で足りる。load_file と save_file は **同時に使わない**
 * ので、関数ごとに持つと var スロットが 2 つ要るだけ。 */
static FILE *vfp;
static int  pending;        /* getkey の 1 文字戻しバッファ。-1=無し */

/* ---- undo(4 段・コマンド単位)----
 *   本文を変えたコマンドを 4 回ぶんまで遡って取り消せる。履歴はリングで、
 *   5 回目を積むと一番古いものが落ちる。
 *     kind 1 = 「消した」の取り消し → 記録した内容を pos へ挿し戻す
 *     kind 2 = 「入れた」の取り消し → pos から len バイトを消す(内容は不要)
 *   記録は **追加ブロックの末尾**に置く ── 像を 1 バイトも食わないため。
 *   1 段 = 68B(kind 1 / len 1 / pos 2 / 内容 64)。消した量が 64B を超える
 *   ときは正しく戻せないので、その場で履歴を捨てる(中途半端に戻さない)。
 *   段の場所はポインタ urec で持つ ── tzcc の配列添字は変数だと 1 回 11B
 *   かかるので、固定添字で触れるポインタの方が安い。 */
#define UNDO_N   4
#define USTRIDE  68
#define UMAXLEN  64
#define UTOTAL   272        /* UNDO_N * USTRIDE。積の定数畳み込みを当てにしない */
#define ULAST    204        /* (UNDO_N-1) * USTRIDE */
static char *ubase;         /* 記録領域の先頭 */
static char *urec;          /* 次に積む段 */
static unsigned char ucnt;  /* 積まれている段数 */
static int  ins_org;        /* 挿入を始めた位置 */
static int  ins_tl;         /* 挿入を始めたときの tlen */
static unsigned char ins_ok;/* 1 = この挿入は ins_org からの純粋な追加 */

static char excmd[8];       /* ':' の入力バッファ。コマンドは 2 文字以内 */

/* ---- 端末制御 ---- */

/* ESC + s を出す。tzcc の文字列リテラルに \033 を書かずに済ませる。 */
static void esc(char *es_s)
{
    putchar(27);
    prs(es_s);
}

/* scr_row / scr_col(1-based)へカーソルを動かす */
static void mvcur(void)
{
    putchar(27);
    putchar('[');
    prnum(scr_row);
    putchar(';');
    prnum(scr_col);
    putchar('H');
}

/* ---- 行の境界 ---- */

/* lh_p を含む行の行頭 */
static int line_head(int lh_p)
{
    /* 後方走査は **CPDR 1 命令**(#43)。C の while は 1 文字あたり
     * 25 命令 250～300 T-state 払っていた。absrscan は「最後の \n の
     * 1 つ次」= 行頭を返すのでそのまま使える。 */
    lh_p = (int)absrscan((unsigned)text + lh_p, 10, lh_p);
    return lh_p - (int)text;
}

/* lt_p を含む行の行末('\n' の位置、最終行なら tlen)*/
static int line_tail(int lt_p)
{
    /* 前方走査は **CPIR 1 命令**(#43)。上限の判定を残してあるのは
     * 旧実装と違い **n が負だと 64KB 走査してしまう**から。 */
    if (lt_p >= tlen) return tlen;
    lt_p = (int)absscan((unsigned)text + lt_p, 10, tlen - lt_p);
    return lt_p - (int)text;
}

/* 次の行頭。無ければ -1。**呼び出しは常に cur なので引数を取らない。**
 * tzcc は引数 1 個につき関数入口で ~17B 払うので、呼び出しが 1〜2 箇所しか
 * 無い関数は引数を無くす方が得(逆に呼び出しが多い関数は push が安いので
 * 引数のままの方がよい ── line_head / line_tail はそちら)。 */
static int line_next(void)
{
    int lnx_t;
    lnx_t = line_tail(cur);
    if (lnx_t >= tlen) return -1;
    return lnx_t + 1;
}

/* 前の行頭。無ければ -1(同上、引数なし)。 */
static int line_prev(void)
{
    int lpv_h;
    lpv_h = line_head(cur);
    if (lpv_h == 0) return -1;
    /* ★変数へ受けてから返す。`return line_head(lpv_h - 1);` と書くと
     * **tzcc がこの文を丸ごと黙って捨てていた**(戻り値が call そのものの形)。
     * アセンブラには `Lend10: ret` だけが残り、line_prev が「いまの行頭」
     * を返し、**k / 上カーソルが無反応**になっていた。
     * tzcc 側は修正済(NODE_RETURN で NODE_CALL を扱う + 未対応形は
     * エラーで落とす)だが、安い形なのでこのままにしてある。 */
    lpv_h = line_head(lpv_h - 1);
    return lpv_h;
}

/* la_n 行目(1-based)の行頭。超えていたら最終行。
 *   `12G` / `12gg` 用。行配列を持たない設計なので先頭から数えるが、
 *   1 行分の走査は CPIR 1 命令(#43)なので 200 行でも瞬時。 */
static int line_at(int la_n)
{
    int la_p;

    la_p = 0;
    while (la_n > 1) {
        if (la_p >= tlen) break;        /* 行数を超えた = 最終行で止める */
        la_p = line_tail(la_p) + 1;
        la_n--;
    }
    if (la_p > tlen) la_p = tlen;
    return line_head(la_p);
}

/* ml_start(目標行の行頭)へ、いまの桁をできるだけ保って移動する。
 * j / k / 上下カーソルの共通処理。ml_start < 0(行が無い)なら何もしない。 */
static void move_line(int ml_start)
{
    int ml_col;
    int ml_t;

    if (ml_start < 0) return;
    ml_col = cur - line_head(cur);
    ml_t = line_tail(ml_start);
    if (ml_start + ml_col > ml_t) cur = ml_t;
    else cur = ml_start + ml_col;
}

/* top から数えた cur の画面行(1-based)。画面下にはみ出すと TROWS より大きい */
static int row_of_cur(void)
{
    int roc_h;
    int roc_i;
    unsigned char roc_r;  /* 画面行(1..46)*/

    roc_h = line_head(cur);
    roc_i = top;
    roc_r = 1;
    while (roc_i < roc_h) {
        roc_i = line_tail(roc_i) + 1;
        roc_r++;
        if (roc_r > TROWS + TROWS) break;   /* 保険(壊れた top で回り続けない)*/
    }
    return roc_r;
}

/* cur が画面に入るよう top を動かす。動かしたら 1 を返す(要再描画)*/
static int scroll_fix(void)
{
    unsigned char sf_moved;

    sf_moved = 0;
    if (cur < top) {
        top = line_head(cur);
        return 1;
    }
    for (;;) {
        if (row_of_cur() <= TROWS) break;
        top = line_tail(top) + 1;
        sf_moved = 1;
    }
    return sf_moved;
}

/* ---- 描画 ---- */

static void place(void)
{
    unsigned char pc_c;   /* 桁(1..79)*/

    scr_row = row_of_cur();
    pc_c = cur - line_head(cur) + 1;
    if (pc_c > TCOLS) pc_c = TCOLS;
    scr_col = pc_c;
    mvcur();
}

/* 最下行(24 行目)へ移動してステータスを引き直す。
 * カーソル移動だけのキーではここと place() しか出さない ── 全画面 24 行を
 * 毎キー描くと、cpmsim では気にならないが実機のシリアル(115200)では
 * 1 キーあたり 2KB ≒ 170ms かかる。 */
static void status(void)
{
    unsigned char st_s;

    /* 乗算を書かない掟なので 4*msgid は加算で組む。 */
    st_s = msgid + msgid;
    st_s = st_s + st_s + mode + mode + dirty;
    if (st_s == stold) return;      /* 内容が同じ = 1 バイトも出さない */
    stold = st_s;

    scr_row = TROWS + 1;
    scr_col = 1;
    mvcur();
    esc("[K");
    prs(fname);
    if (dirty) prs(" [+]");

    if (mode) prs("  -- INSERT --");
    /* メッセージは 3 種に絞ってある。「保存できなかった」系は理由を分けずに
     * 1 本にまとめた。
     * ★switch ではなく if の連鎖(do_cmd と同じ理由。tzcc の switch は
     *   case 本体への PIC 間接ジャンプと break のジャンプを払う)。#69 で
     *   core を詰めるときに書き換えた。 */
    if (msgid == 1) prs("  written");
    else if (msgid == 2) prs("  modified (:q!)");
    else if (msgid == 3) prs("  cannot write");
    /* 6(buffer full)は 0 に寄せた。ins_at が黙って拒否する = 打っても
     * 文字が増えないので、それ自体が合図になる。
     * 4(大きすぎて保存不可)と 5(新規ファイル)は 3 と 0 に寄せた */
}

/* pt_p から 1 行出し、その行末(改行の位置、最終行なら tlen)を返す。
 *   80 桁を超えた分は出さないが、行末を探すために走査は続ける。
 *   render(全面)と render_line(1 行)の両方から呼ぶ ── 写しを
 *   2 つ持つとそれだけで ~250B。 */
static int put_line(int pt_p)
{
    unsigned char pt_c;
    unsigned char pt_ch;

    pt_c = 0;
    while (pt_p < tlen) {
        pt_ch = text[pt_p];
        if (pt_ch == '\n') break;
        if (pt_c < TCOLS) {
            putchar(pt_ch);
            pt_c++;
        }
        pt_p++;
    }
    return pt_p;
}

static void render(void)
{
    int rd_i;       /* 表示中の行頭 */
    int rd_j;       /* いま出した行の行末 */
    unsigned char rd_r;   /* 画面行(0-based。0..22)*/
    unsigned char rd_live; /* まだ本文が残っている */

    esc("[H");
    rd_i = top;
    rd_r = 0;
    rd_live = 1;
    while (rd_r < TROWS) {
        if (rd_r) putchar('\n');       /* 行間だけ改行。最終行では出さない */
        esc("[K");
        if (rd_live) {
            rd_j = put_line(rd_i);
            if (rd_j >= tlen) rd_live = 0;      /* いま出したのが最終行 */
            else rd_i = rd_j + 1;
        } else {
            putchar('~');
        }
        rd_r++;
    }
    status();       /* status() が 24 行目へ絶対移動する */
    place();
}

/* いまの行 1 本だけ書き直す。
 *   文字を打つ / x で消すだけなら他の 22 行は 1 ドットも変わらないのに、
 *   全 23 行を ESC[K で消して書き直していた。消し→書きの間が見えるのが
 *   チラつきの正体なので、触った行だけにする。 */
static void render_line(void)
{
    scr_row = row_of_cur();
    scr_col = 1;
    mvcur();
    esc("[K");
    put_line(line_head(cur));
    status();
    place();
}

/* ---- 本文の編集 ---- */

/* ia_pos に 1 文字挿入。入らなければ 0 */
static int ins_at(int ia_pos, int ia_ch)
{
    if (tlen >= tmax) {
        /* バッファ満杯: 黙って拒否(打っても増えないのが合図)*/
        return 0;
    }
    /* 巻き上げは **LDDR**(#43)。重なりがあって dst > src なので
     * absmove(LDIR)だと自分が書いた先を読んで壊れる。 */
    absmovd((unsigned)text + ia_pos + 1, (unsigned)text + ia_pos,
            (unsigned)(tlen - ia_pos));
    text[ia_pos] = (char)ia_ch;
    tlen++;
    dirty = 1;
    touched = 1;
    if (ia_ch == '\n') lnr = 2;       /* 行が増える = 下が全部ずれる */
    else if (lnr == 0) lnr = 1;
    return 1;
}

/* da_pos の 1 文字を削除 */
static void del_at(int da_pos)
{
    if (da_pos >= tlen) return;    /* 呼び出し側が負を渡さないので下限は見ない */
    if (text[da_pos] == '\n') lnr = 2;    /* 行が繋がる = 下が全部ずれる */
    else if (lnr == 0) lnr = 1;
    absmove((unsigned)text + da_pos, (unsigned)text + da_pos + 1,
            (unsigned)(tlen - da_pos - 1));      /* 後ろを詰める = LDIR */
    tlen--;
    dirty = 1;
    touched = 1;
}

/* cur の行を yank へ写す(改行は含めない)*/
static void yank_line(void)
{
    int yl_h;
    int yl_t;

    yl_h = line_head(cur);
    yl_t = line_tail(cur) - yl_h;        /* 行の長さへ */
    if (yl_t > YANK_MAX - 1) yl_t = YANK_MAX - 1;   /* 打ち切り */
    absmove((unsigned)yank, (unsigned)text + yl_h, (unsigned)yl_t);
    ylen = (unsigned char)yl_t;
}

/* 1 段積む。kind 1 なら text[upos..upos+ulen) を記録に写す。 */
static void urecord(unsigned char ur_kind, int ur_pos, int ur_len)
{
    if (ur_len == 0) return;                /* 何も変わっていない */
    if (ur_len > UMAXLEN) {                 /* 戻しきれない = 履歴ごと捨てる */
        ucnt = 0;
        urec = ubase;
        return;
    }
    urec[0] = (char)ur_kind;
    urec[1] = (char)ur_len;
    pokew((unsigned)urec + 2, (unsigned)ur_pos);
    /* 内容の写しは LDIR 1 発(absmove)。自前ループは ~150B 太る。 */
    if (ur_kind == 1) absmove((unsigned)urec + 4, (unsigned)text + ur_pos, ur_len);
    urec = urec + USTRIDE;
    if (urec >= ubase + UTOTAL) urec = ubase;   /* リング */
    if (ucnt < UNDO_N) ucnt++;
}

/* 1 段戻す(u)。
 *   ★挿し戻し / 消しは **ins_at / del_at を 1 文字ずつ呼ぶ**。自前で本文を
 *   ずらすコードを書くと像が ~450B 太る(実測)。undo は 1 打鍵に 1 回しか
 *   走らないので、O(len x tlen) の移動時間の方を捨てる。 */
static void do_undo(void)
{
    int ud_k;
    int ud_pos;
    int ud_len;

    if (ucnt == 0) return;
    if (urec == ubase) urec = ubase + ULAST;
    else urec = urec - USTRIDE;
    ucnt--;

    ud_len = (int)(unsigned char)urec[1];
    ud_pos = (int)peekw((unsigned)urec + 2);

    if (urec[0] == 2) {                     /* 入れたものを消す */
        while (ud_len > 0) {
            del_at(ud_pos);
            ud_len--;
        }
    } else {                                /* 消したものを挿し戻す */
        ud_k = 0;
        while (ud_k < ud_len) {
            ins_at(ud_pos + ud_k, urec[4 + ud_k]);
            ud_k++;
        }
    }
    cur = ud_pos;
    touched = 1;
}


/* yank の内容を下の行へ貼る(p)*/
static void paste_line(void)
{
    int pl_at;
    unsigned char pl_need; /* 改行 + ヤンク行の長さ(1..97)*/

    if (ylen == 0xFF) return;            /* 未ヤンク */
    pl_need = ylen + 1;                  /* 改行 + 本文 */
    if (tlen + pl_need > tmax) return;   /* 入らないなら黙って何もしない */

    /* ずらしも写しもブロック命令 1 発ずつ(#43)。 */
    pl_at = line_tail(cur);
    urecord(2, pl_at, pl_need);
    absmovd((unsigned)text + pl_at + pl_need, (unsigned)text + pl_at,
            (unsigned)(tlen - pl_at));          /* 巻き上げ = LDDR */
    text[pl_at] = '\n';
    absmove((unsigned)text + pl_at + 1, (unsigned)yank, (unsigned)ylen);
    tlen = tlen + pl_need;
    dirty = 1;
    touched = 1;
    cur = pl_at + 1;
}

/* ---- ファイル ---- */

static void load_file(void)
{

    int lf_n;

    tlen = 0;
    toobig = 0;
    vfp = fopen(fname, "r");
    if (vfp == NULL) {
        msgid = 0;   /* 新規ファイルは無言(ステータスにファイル名が出る) */
        return;
    }
    lf_n = fread(text, 1, tmax, vfp);
    /* fread は読めた個数しか返さないので負の判定は置かない */
    tlen = lf_n;
    if (lf_n == tmax) {
        /* まだ続きがあるなら本文を全部持てていない = 保存させない */
        if (fgetc(vfp) != EOF) toobig = 1;
    }
    fclose(vfp);

    /* 末尾の改行は落とす(行の数え方を「区切りは 1 個」に統一する)*/
    if (tlen > 0) {
        if (text[tlen - 1] == '\n') tlen--;
    }
}

static void save_file(void)
{


    if (toobig) {
        msgid = 3;   /* 大きすぎて保存不可も「書けない」に寄せる */
        return;
    }
    vfp = fopen(fname, "w");
    if (vfp == NULL) {
        msgid = 3;
        return;
    }
    if (tlen > 0) {
        fwrite(text, 1, tlen, vfp);
        fputc('\n', vfp);             /* 最終行にも改行を付ける */
    }
    fclose(vfp);
    dirty = 0;
    msgid = 1;
}

/* ---- 入力 ---- */

/* キー入力。ESC シーケンスを見てカーソルキーを拾う。
 *   ESC の続きが '[' でなければ素の ESC(挿入モードを抜ける)。
 *   取りこぼした 1 文字は pending に戻して次の getkey で処理する。 */
static int getkey(void)
{
    int gk_c;
    int gk_c1;
    int gk_c2;

    if (pending >= 0) {
        gk_c = pending;
        pending = -1;
        return gk_c;
    }
    gk_c = getchar();
    if (gk_c != 27) return gk_c;

    gk_c1 = getc_timeout(10);
    if (gk_c1 != '[') {
        if (gk_c1 >= 0) pending = gk_c1;
        return 27;
    }
    gk_c2 = getc_timeout(10);
    if (gk_c2 == 'A') return K_UP;
    if (gk_c2 == 'B') return K_DOWN;
    if (gk_c2 == 'C') return K_RIGHT;
    if (gk_c2 == 'D') return K_LEFT;
    return 27;
}

/* ':' コマンドを 1 行読んで実行する */
static void ex_line(void)
{
    unsigned char ex_n;   /* excmd の長さ(0..6)*/
    unsigned char ex_c;

    ex_n = 0;
    excmd[0] = 0;
    scr_row = TROWS + 1;
    scr_col = 1;
    mvcur();
    esc("[K");
    putchar(':');
    stold = 0xFF;       /* 24 行目を自分で上書きしたので必ず引き直す */

    for (;;) {
        ex_c = getkey();
        if (ex_c == 27) {                       /* ESC = 取り消し */
            excmd[0] = 0;
            return;
        }
        if (ex_c == '\r' || ex_c == '\n') break;
        if (ex_c == 8 || ex_c == 127) {         /* BS */
            if (ex_n == 0) {
                excmd[0] = 0;
                return;
            }
            ex_n--;
            excmd[ex_n] = 0;
            putchar(8);
            putchar(' ');
            putchar(8);
            continue;
        }
        if (ex_c >= 32 && ex_c < 127 && ex_n < 6) {
            excmd[ex_n] = (char)ex_c;
            ex_n++;
            excmd[ex_n] = 0;
            putchar(ex_c);
        }
    }
}

/* excmd[] に読んだ ':' コマンドを実行する。
 *   読む(ex_line)と実行する(ここ)を分けてあるのは、z80 のビルドでそれぞれを
 *   別のオーバーレイにするため(#69。1 本だと 1.1KB あり、オーバーレイ領域の
 *   大きさ = 最大のオーバーレイ を押し上げていた)。C としては普通の 2 関数。
 *   ESC / BS で取り消したときは excmd[0] が 0 なので何もしない。 */
static void ex_run(void)
{
    unsigned char xr_c;
    unsigned char xr_n;

    /* q / q! / w / wq / x。文字数が少ないので strcmp は引かない。
     * excmd[] の添字読みは 1 回ずつに畳む(tzcc は 1 アクセスごとに実費)。 */
    xr_c = excmd[0];
    xr_n = excmd[1];

    if (xr_c == 'q') {
        if (xr_n == '!') quitf = 1;
        else if (xr_n != 0) return;         /* q に続く未知の文字 */
        else if (dirty) msgid = 2;
        else quitf = 1;
        return;
    }
    if (xr_c == 'w') {
        if (xr_n != 0 && xr_n != 'q') return;
        save_file();
        if (xr_n == 'q' && msgid == 1) quitf = 1;
        return;
    }
    /* `:x` は外した(`:wq` と同義で、case 1 個ぶんの代金だけ払っていた)。 */
}

/* dd(2 打鍵目を読んでから)。ol_n はカウント接頭辞(0 = 無し)。
 *   do_cmd から括り出してあるのは #69 でオーバーレイにするため。 */
static void op_dd(unsigned char ol_n)
{
    int dl_h;
    int dl_t;

    if (getkey() != 'd') return;
    if (ol_n == 0) ol_n = 1;
    /* cur の行を消す(yank にも入れる)を ol_n 回。3dd = 3 行削除。
     *   もとは del_line() という別関数だったが、呼ぶのがここだけになったので
     *   取り込んだ(#69。呼び出し 1 回と関数の入口のぶん詰まる)。
     *   yank_line() を呼ばずに写しを自前で持っているのも #69 のため ──
     *   z80 のビルドでは dd と yy が別々のオーバーレイになり、オーバーレイどうしは
     *   呼び合えない。行頭・行末はどのみちここで求めるので、写しは absmove 1 発。 */
    while (ol_n) {
        dl_h = line_head(cur);
        dl_t = line_tail(cur);
        ylen = YANK_MAX - 1;             /* 打ち切り(yank_line と同じ規則)*/
        if (dl_t - dl_h < YANK_MAX - 1) ylen = (unsigned char)(dl_t - dl_h);
        absmove((unsigned)yank, (unsigned)text + dl_h, (unsigned)ylen);
        if (dl_t < tlen) dl_t++;        /* 行末の改行も消す */
        else if (dl_h > 0) dl_h--;      /* 最終行なら手前の改行を消す */

        urecord(1, dl_h, dl_t - dl_h);

        absmove((unsigned)text + dl_h, (unsigned)text + dl_t,
                (unsigned)(tlen - dl_t));       /* 残りを詰める = LDIR */
        tlen = dl_h + tlen - dl_t;
        dirty = 1;
        touched = 1;
        if (dl_h > tlen) dl_h = tlen;
        cur = line_head(dl_h);
        ol_n--;
    }
}

/* yy(同上)。yank は 1 行分しか持てないのでカウントは見ない。 */
static void op_yy(void)
{
    if (getkey() == 'y') yank_line();
}

/* e: 語末へ。el_t はいまの行末。カウントは dd 専用なのでここでは 1 回だけ。
 *   'w'(語頭へ)は使わないとのことで外してある。
 *   do_cmd から括り出してあるのは #69 でオーバーレイにするため。 */
static void mv_end(int el_t)
{
    if (cur + 1 < el_t) cur++;
    while (cur + 1 < el_t && text[cur + 1] != ' ') cur++;
}

/* 1..9 はカウント接頭辞(3dd / 12j / 5x …)。乗算を書かない掟なので
 *   10 倍は 8x+2x を加算で組む。200 で頭打ち(char に収める)。
 *   do_cmd から括り出してあるのは #69 でオーバーレイにするため。 */
static void cnt_digit(unsigned char cd_k)
{
    int cd_2;
    int cd_4;

    cd_2 = count + count;                   /* 2x */
    cd_4 = cd_2 + cd_2;                     /* 4x */
    cd_4 = cd_4 + cd_4 + cd_2 + (cd_k - '0');   /* 10x + 桁 */
    if (cd_4 > 200) cd_4 = 200;             /* 暴走防止 */
    count = cd_4;
}

/* G / gg。gl_n はカウント接頭辞(0 = 無し)。
 *   12G = 12 行目 / G = 最終行。画面の追従は scroll_fix に任せる
 *   (同じ画面内への移動なら描き直す必要が無い)。
 *   do_cmd から括り出してあるのは #69 でオーバーレイにするため。 */
static void go_line(unsigned char gl_k, unsigned char gl_n)
{
    if (gl_k == 'G') {
        if (gl_n) cur = line_at(gl_n);
        else cur = line_head(tlen);
        return;
    }
    /* ★touched を立てないと画面が更新されない。top を自分で 0 にするので
     * scroll_fix が「動いた」と判定できず、カーソルだけ左上へ行って
     * 本文が古いまま残る = 一見「gg が効かない」ように見えた。 */
    if (getkey() == 'g') {
        if (gl_n) cur = line_at(gl_n);      /* 12gg = 12 行目 */
        else { cur = 0; top = 0; }          /* gg = 先頭 */
        touched = 1;
    }
}

/* ---- コマンドモード ---- */
static void do_cmd(unsigned char dc_k)
{
    int dc_h;                   /* いまの行頭。switch に入る前に 1 回だけ求める */
    int dc_t;                   /* いまの行末(同上)                          */
    unsigned char dc_n;         /* カウント接頭辞の回数(既定 1)             */

    msgid = 0;

    /* カーソルキーは伝統キーへ畳んでから switch する。
     * case ラベルを 5 個減らすため ── tzcc の switch は 1 ラベルにつき
     * 「PIC 間接ジャンプ 15 命令」を払う(関数が大きく jr の射程に入らない)。 */
    if (dc_k == K_LEFT)  dc_k = 'h';
    if (dc_k == K_RIGHT) dc_k = 'l';
    if (dc_k == K_UP)    dc_k = 'k';
    if (dc_k == K_DOWN)  dc_k = 'j';

    if (dc_k == 12) { touched = 1; stold = 0xFF; return; }  /* Ctrl+L = 全部作り直す */

    /* 1..9 はカウント接頭辞(3dd / 12j / 5x …)。'0' は行頭コマンドなので
     * カウントの途中でだけ数字として扱う ── 本家 vi と同じ規則。 */
    if (dc_k >= '0' && dc_k <= '9' && (dc_k != '0' || count != 0)) {
        cnt_digit(dc_k);
        return;                             /* 次のキーを待つ(画面は変えない)*/
    }
    /* ★dc_n は **0 = 数字を打っていない** という意味で持つ。
     *   G / gg は「数字あり = N 行目 / 無し = 最終行(先頭)」と
     *   振る舞いが変わるので、ここで 1 に丸めてしまうと `1G` と区別が付かない。 */
    dc_n = count;
    count = 0;

    /* 2 打鍵のコマンド(dd / yy / gg)と G は switch へ入れずここで捌く。
     * case ラベル 1 個 ≒ 24B(PIC 間接ジャンプ)に対し if は ~10B。
     * どれも行頭・行末を使わないので、下の 2 回の呼び出しより前で済む。 */
    if (dc_k == 'd') { op_dd(dc_n); return; }
    if (dc_k == 'y') { op_yy(); return; }
    if (dc_k == 'u') { do_undo(); return; }
    if (dc_k == 'g' || dc_k == 'G') { go_line(dc_k, dc_n); return; }

    /* ★行頭・行末は **ここで 1 回だけ**求める。
     *   以前は case ごとに line_head(cur) / line_tail(cur) を呼び直していて、
     *   tzcc では 1 呼び出しが「引数 push + call + IX 張り直し」≒ 30B。
     *   12 箇所 → 2 箇所にして ~300B 減った。cur を書き換える case は
     *   いずれも書き換える **前** にしか使わないので、これで正しい。 */
    dc_h = line_head(cur);
    dc_t = line_tail(cur);

    /* ★case ラベルではなく if の連鎖で捌く。
     *   tzcc の switch は「dispatch の cp/jr(5B)」の後に **case 本体への PIC
     *   間接ジャンプ(18B)** と **break の PIC 間接ジャンプ(18B)** を払う
     *   (do_cmd が大きく、jr の射程 ±127B に本体も末尾も入らないため)。
     *   一方 if は比較 14B + ret 1B。**1 コマンドあたり ~26B 安い。**
     *   ※ 比較が 14B で済むのは tzcc 側の覗き穴最適化(peep.py)以降。
     *   並び順は打鍵頻度順(移動キーが先)。 */
    if (dc_k == 'h') { if (cur > dc_h) cur--; return; }

    /* コマンドモードのカーソルは [行頭, 行末-1] にしか居られない */
    if (dc_k == 'l') { if (cur + 1 < dc_t) cur++; return; }

    if (dc_k == 'k') { move_line(line_prev()); return; }
    if (dc_k == 'j') { move_line(line_next()); return; }
    if (dc_k == '0') { cur = dc_h; return; }

    if (dc_k == '$') {
        if (dc_t > dc_h) cur = dc_t - 1;
        else cur = dc_t;
        return;
    }

    if (dc_k == 'e') { mv_end(dc_t); return; }

    if (dc_k == 'x') {
        if (dc_t > dc_h && cur < dc_t) {
            urecord(1, cur, 1);
            del_at(cur);
        }
        return;
    }

    if (dc_k == 'p') { paste_line(); return; }

    if (dc_k == ':') {
        ex_line();
        ex_run();
        touched = 1;        /* 最下行を直接書いたので画面を作り直す */
        return;
    }

    /* ここから挿入モードに入る 4 つ。どれも「挿入の開始位置」を控えてから
     * mode を立てる ── undo(#39)が i..ESC を 1 段として積むため。 */
    if (dc_k == 'a') {
        if (cur < dc_t) cur++;
    } else if (dc_k == 'o') {
        cur = dc_t;
    } else if (dc_k == 'O') {
        /* 行頭に改行を挿すと元の行が 1 つ下へ落ち、cur は空行の上に残る */
        cur = dc_h;
    } else if (dc_k != 'i') {
        return;                         /* 知らないキー */
    }
    ins_org = cur;
    ins_tl = tlen;
    ins_ok = 1;
    if (dc_k == 'o') {
        if (ins_at(cur, '\n')) cur++;
    } else if (dc_k == 'O') {
        ins_at(cur, '\n');
    }
    mode = 1;
}

/* ---- 挿入モード ---- */
static void do_ins(unsigned char di_k)
{
    /* 挿入中のカーソルキー。縦移動は無効(簡易版)なので捨てる。 */
    if (di_k == K_UP || di_k == K_DOWN) return;
    /* 挿入中の移動は「ins_org からの連続な追加」という前提を壊す */
    if (di_k == K_LEFT)  { ins_ok = 0; if (cur > line_head(cur)) cur--; return; }
    if (di_k == K_RIGHT) { ins_ok = 0; if (cur < line_tail(cur)) cur++; return; }
    if (di_k == 9) di_k = 0x20;   /* TAB は空白 1 個に落とす */

    /* ★switch ではなく if の連鎖(理由は do_cmd と同じ)。
     *   並び順は打鍵頻度順 ── 普通の文字が圧倒的に多い。 */
    if (di_k >= 32 && di_k < 127) {
        if (ins_at(cur, di_k)) cur++;
        return;
    }

    if (di_k == '\r' || di_k == '\n') {
        if (ins_at(cur, '\n')) cur++;
        return;
    }

    if (di_k == 8 || di_k == 127) {         /* BS */
        if (cur > 0) {
            cur--;
            if (cur < ins_org) ins_ok = 0;   /* 挿入前からあった文字を消した */
            del_at(cur);
        }
        return;
    }

    if (di_k == 27) {                       /* ESC でコマンドモードへ */
        mode = 0;
        /* i..ESC を 1 段として積む。ins_ok が降りているときは
         * その範囲が純粋な追加では無い = 正しく戻せないので
         * 中途半端に戻すより履歴を捨てる。 */
        if (ins_ok && tlen > ins_tl) urecord(2, ins_org, tlen - ins_tl);
        else if (tlen != ins_tl) { ucnt = 0; urec = ubase; }
        if (cur > line_head(cur)) cur--;
    }
}

/* 起動時の準備。0 = 続行 / 1 = 終了。
 *   main から括り出してあるのは #69 で z80 のビルドがオーバーレイにするため
 *   (1 回しか走らないコードを常駐させる理由が無い)。 */
static int vi_init(int vn_argc, char **vn_argv)
{
    unsigned char mn_i;   /* fname の長さ(0..39)*/
    char *mn_p;

    if (vn_argc < 1 || vn_argv[0] == 0) {
        puts("usage: vi FILE");
        return 1;
    }

    /* 本文バッファ = 追加ブロック(#38)。像には持たない。
     * 0 が返るなら .BIN のヘッダに追加ブロックが刻まれていない
     * (make tizixcmd CMD=vi XBLK=1 でビルドすること)。 */
    text = (char *)getxbase();
    tmax = (int)getxsize();
    if (tmax == 0) {
        puts("vi: no xblk");
        return 1;
    }
    /* 追加ブロックの **上端はスタックと argv[] が使う**(SP はプロセス最上端から
     * 下りてくる)。FatFs を叩くので 1.5KB 残す。[[kexec-block-headroom]] */
    tmax = tmax - VI_STACK_RESERVE;
    /* undo の記録は本文バッファの直上(追加ブロック内)に置く。
     * 像(3 ブロック)を 1 バイトも食わないため。本文は 2560 -> 2288B。 */
    tmax = tmax - UTOTAL;
    ubase = text + tmax;
    urec = ubase;
    /* 下限チェックは置かない。追加ブロックは 4096B 固定(XBLK=1)なので
     * tmax は必ず 2560 になり、起こりえない分岐にコードを払う意味が無い。 */

    /* argv[0][i] は二重間接で高い。ポインタに落としてから回す。 */
    mn_p = vn_argv[0];
    mn_i = 0;
    while (mn_i < NAME_MAX - 1) {
        if (mn_p[mn_i] == 0) break;
        fname[mn_i] = mn_p[mn_i];
        mn_i++;
    }
    fname[mn_i] = 0;

    /* 0 で始まる大域(tlen/cur/top/mode/dirty/quitf/msgid)は初期化しない。
     * kexec は .BIN を毎回そのままブロックへ読み込むので、_DATA は
     * リンク時の値(= 0)で始まる。ゼロ代入はそのぶん丸ごと無駄。
     * ただし **追加ブロックは初期化されない**ので tlen は明示的に 0 にする
     * (load_file が必ず設定するが、意図を残す)。 */
    ylen = 0xFF;
    pending = -1;
    stold = 0xFF;       /* 起動直後の 1 回は必ず書く */
    tlen = 0;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char mn_k;   /* 打鍵 */

    if (vi_init(argc, argv)) return 1;
    load_file();

    esc("[2J");
    render();

    while (quitf == 0) {
        mn_k = getkey();
        lnr = 0;
        if (mode) do_ins(mn_k);
        else do_cmd(mn_k);

        /* カーソルの正規化はここに集約する(各コマンドに書くと
         * 同じ 3 行が 10 箇所に散って、そのぶん丸ごとコードが増える)。
         * コマンドモードのカーソルは行末('\n' の位置)に乗れない。 */
        if (cur > tlen) cur = tlen;
        /* cur は負にならないので下限は見ない。
         * ※ #42: 「`x < 0` は tzcc で永久に偽」と書いてあったが誤り。
         * 定数 0 との `< 0` は **`and #0x80` で符号ビットを見る専用コード**
         * が出て正しく動く。符号なしになるのは **変数どうし / 0 以外の定数**。 */
        if (mode == 0) {
            if (cur > line_head(cur) && cur == line_tail(cur)) cur--;
        }
        if (top > tlen) top = 0;
        top = line_head(top);

        /* 本文が変わったか画面がスクロールしたときだけ 24 行を描き直す。
         * カーソル移動やモード切替はステータス行 + カーソル移動だけで済む
         * (実機のシリアルでは 1 打鍵 2KB が 40B になる)。 */
        if (scroll_fix()) {
            touched = 1;
            lnr = 2;            /* 画面が動いたなら全面 */
        }
        if (touched) {
            /* 行内だけならその行だけ。実測で 1 打鍵 167B → 行長 + ~30B。 */
            if (lnr == 1) render_line();
            else render();
            touched = 0;
        } else {
            status();
            place();
        }
    }

    /* 終了時は消すだけ。ホームへ戻す ESC[H は省いた(直後に sh が
     * プロンプトを出すので実害が無く、呼び出し 1 回分 ~24B が浮く)。 */
    esc("[2J");
    return 0;
}
