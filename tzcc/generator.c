#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "generator.h"

static Node *root_node = NULL;

// 戻り値: 0=未検出 / 1=char スカラー(1byte) / 2=配列 / 3=ポインタ /
//         4=int スカラー(2byte) / 5=long スカラー(4byte)
static int find_sym_type(Node *node, const char *name) {
    if (!node || !name) return 0;
    if ((node->type == NODE_VAR_DECL || node->type == NODE_ARRAY_DECL || node->type == NODE_PTR_DECL) && node->value) {
        if (strcmp(node->value, name) == 0) {
            if (node->type == NODE_PTR_DECL) return 3;
            if (node->type == NODE_ARRAY_DECL) return 2;
            if (node->base_type == 2) return 5;
            return node->base_type == 1 ? 4 : 1;
        }
    }
    int t = find_sym_type(node->left, name);
    if (t) return t;
    t = find_sym_type(node->right, name);
    if (t) return t;
    t = find_sym_type(node->third, name);
    if (t) return t;
    t = find_sym_type(node->fourth, name);
    if (t) return t;
    return find_sym_type(node->next, name);
}

// stdin/stdout/stderr は crt0.s 側のグローバル (_stdin など) を直接参照する
static int is_std_stream(const char *name) {
    if (!name) return 0;
    return strcmp(name, "stdin") == 0 || strcmp(name, "stdout") == 0 || strcmp(name, "stderr") == 0;
}

// ============================================================
//  32bit (long) サポート  ── 値は DE:HL (DE=上位16 / HL=下位16)
// ============================================================

// 関数 name の戻り値が long か（main.c のレジストリ。定義・プロトタイプ両対応）
extern int func_returns_long(const char *name);
static int find_func_is_long(Node *node, const char *name) {
    (void)node;
    return func_returns_long(name);
}

// ポインタ変数 name が long*（指す先が 32bit）か
static int find_ptr_is_long(Node *node, const char *name) {
    if (!node || !name) return 0;
    if (node->type == NODE_PTR_DECL && node->value && strcmp(node->value, name) == 0)
        return node->base_type == 2;
    int t = find_ptr_is_long(node->left, name);  if (t) return t;
    t = find_ptr_is_long(node->right, name);     if (t) return t;
    t = find_ptr_is_long(node->third, name);     if (t) return t;
    t = find_ptr_is_long(node->fourth, name);    if (t) return t;
    return find_ptr_is_long(node->next, name);
}

// name が「ポインタの配列 / ポインタへのポインタ」か（char **argv / char *argv[] /
// char *w[N]）。この compiler では PTR_DECL・ARRAY_DECL の base_type==1 がその印
// （要素は 2 バイトのポインタ）。一段添字した結果は char*（要素 1 バイト）になる。
static int find_ptr_is_ptrarr(Node *node, const char *name) {
    if (!node || !name) return 0;
    if ((node->type == NODE_PTR_DECL || node->type == NODE_ARRAY_DECL)
        && node->value && strcmp(node->value, name) == 0)
        return node->base_type == 1;
    int t = find_ptr_is_ptrarr(node->left, name);  if (t) return t;
    t = find_ptr_is_ptrarr(node->right, name);     if (t) return t;
    t = find_ptr_is_ptrarr(node->third, name);     if (t) return t;
    t = find_ptr_is_ptrarr(node->fourth, name);    if (t) return t;
    return find_ptr_is_ptrarr(node->next, name);
}

// 配列/ポインタ name の要素バイトサイズ（char=1 / int・ptr=2 / long=4）
static int find_decl_esz(Node *node, const char *name) {
    if (!node || !name) return 0;
    if ((node->type == NODE_VAR_DECL || node->type == NODE_ARRAY_DECL || node->type == NODE_PTR_DECL)
        && node->value && strcmp(node->value, name) == 0)
        return node->base_type == 2 ? 4 : (node->base_type == 1 ? 2 : 1);
    int t = find_decl_esz(node->left, name);  if (t) return t;
    t = find_decl_esz(node->right, name);     if (t) return t;
    t = find_decl_esz(node->third, name);     if (t) return t;
    t = find_decl_esz(node->fourth, name);    if (t) return t;
    return find_decl_esz(node->next, name);
}

// 数値リテラル文字列 (10進 or 0x..) を 32bit へ
static unsigned long num_u32(const char *s) {
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (unsigned long)strtoul(s + 2, NULL, 16);
    return (unsigned long)strtoul(s, NULL, 10);
}

// 式 n が long 値(DE:HL)を生むか
static int expr_is_long(Node *n) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_NUMBER:
            return n->is_long || num_u32(n->value) > 0xFFFFUL;
        case NODE_IDENTIFIER:
            return find_sym_type(root_node, n->value) == 5;
        case NODE_CVT:
            return n->base_type == 2 && n->array_size == 0;  // long へ拡大
        case NODE_ADD: case NODE_SUB:
            return expr_is_long(n->left) || expr_is_long(n->right);
        case NODE_TERNARY:
            return expr_is_long(n->right) || expr_is_long(n->third);
        case NODE_CALL:
            return find_func_is_long(root_node, n->value);
        case NODE_PREINC: case NODE_PREDEC:
        case NODE_POSTINC: case NODE_POSTDEC:
            return n->value && find_sym_type(root_node, n->value) == 5;
        case NODE_DEREF: {
            Node *p = n->left;
            if (p && p->type == NODE_CVT && p->base_type == 2 && p->array_size == 1) return 1;
            if (p && p->type == NODE_IDENTIFIER && find_ptr_is_long(root_node, p->value)) return 1;
            return 0;
        }
        case NODE_INDEX:
            // long 配列 / long* の添字
            return n->left && n->left->type == NODE_IDENTIFIER
                && find_decl_esz(root_node, n->left->value) == 4;
        case NODE_MEMBER:
            return n->esz == 4 && n->base_type != 2;   // long スカラーメンバ
        default: return 0;
    }
}

// var_name(4byte) -> de:hl。--tizix-user の tizix.c は `ld hl,#label`(+IY) と
// レジスタ間接 (hl) しか解釈しないので、その形だけで組む
// (`ld de,(label+2)` / `ld (label+2),de` は tizix.c が壊す)。
static void g32_load_var(const char *name, FILE *o) {
    fprintf(o, "    ld hl, #var_%s\n", name);   // +IY -> 実アドレス
    fprintf(o,
        "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n"  // hl = 下位16
        "    ex de, hl\n"                                            // de = 下位16 (退避)
        "    ld hl, #var_%s\n", name);                               // +IY -> 実アドレス
    fprintf(o,
        "    inc hl\n    inc hl\n"                                   // hl -> +2
        "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n"   // hl = 上位16
        "    ex de, hl\n");                                          // de:hl = 上位:下位
}
static void g32_store_var(const char *name, FILE *o) {
    // de:hl を [base..base+3] へ (little-endian)
    fprintf(o,
        "    push de\n    push hl\n"       // [sp]=下位, [sp+2]=上位
        "    ld hl, #var_%s\n"             // +IY -> 実アドレス
        "    pop bc\n"                     // bc = 下位
        "    ld (hl), c\n    inc hl\n    ld (hl), b\n    inc hl\n"
        "    pop bc\n"                     // bc = 上位
        "    ld (hl), c\n    inc hl\n    ld (hl), b\n", name);
}
static void g32_lit(const char *s, FILE *o) {
    unsigned long v = num_u32(s);
    fprintf(o, "    ld hl, #%u\n    ld de, #%u\n",
            (unsigned)(v & 0xFFFFUL), (unsigned)((v >> 16) & 0xFFFFUL));
}
// hl(16bit, unsigned) を de:hl(32bit) へゼロ拡張
static void g32_widen(FILE *o) { fprintf(o, "    ld de, #0\n"); }
// de:hl を [high][low] の順でスタックへ
static void g32_push(FILE *o) { fprintf(o, "    push de\n    push hl\n"); }

// de:hl(RIGHT) と スタック上の LEFT([high][low]) で 32bit 加減。結果 de:hl。
static void g32_addsub(int is_add, FILE *o) {
    fprintf(o, "    pop bc\n");                 // bc = LEFT.low
    if (is_add) {
        fprintf(o, "    ld a, c\n    add a, l\n    ld l, a\n");
        fprintf(o, "    ld a, b\n    adc a, h\n    ld h, a\n");
        fprintf(o, "    pop bc\n");             // bc = LEFT.high
        fprintf(o, "    ld a, c\n    adc a, e\n    ld e, a\n");
        fprintf(o, "    ld a, b\n    adc a, d\n    ld d, a\n");
    } else {
        fprintf(o, "    ld a, c\n    sub l\n    ld l, a\n");
        fprintf(o, "    ld a, b\n    sbc a, h\n    ld h, a\n");
        fprintf(o, "    pop bc\n");             // bc = LEFT.high
        fprintf(o, "    ld a, c\n    sbc a, e\n    ld e, a\n");
        fprintf(o, "    ld a, b\n    sbc a, d\n    ld d, a\n");
    }
}

// long 式ノードを評価して de:hl に置く（16bit 式なら評価後ゼロ拡張）
static void gen_long(Node *n, FILE *out);

// ============================================================

// ラベル採番（if/while/for/比較演算子で共用）
static int label_id = 0;

// break / continue のジャンプ先スタック（ループ/switch のネスト）
static int g_brk[32];  static int g_brk_sp = 0;
static int g_cont[32]; static int g_cont_sp = 0;

// hl に値を残す「式ノード」か？（代入 RHS / 関数引数 / return で generate_asm を再帰させてよい）
static int is_expr_node(NodeType t) {
    return t == NODE_ADD || t == NODE_SUB || t == NODE_MUL || t == NODE_DIV ||
           t == NODE_EQ  || t == NODE_NE  || t == NODE_LT  || t == NODE_GT  ||
           t == NODE_LE  || t == NODE_GE  || t == NODE_NOT || t == NODE_NEG ||
           t == NODE_INDEX || t == NODE_DEREF || t == NODE_ADDR ||
           t == NODE_PREINC || t == NODE_PREDEC || t == NODE_POSTINC || t == NODE_POSTDEC ||
           t == NODE_AND || t == NODE_OR ||
           t == NODE_BITAND || t == NODE_BITOR || t == NODE_BITXOR || t == NODE_BITNOT ||
           t == NODE_SHL || t == NODE_SHR || t == NODE_MEMBER || t == NODE_TERNARY ||
           t == NODE_CVT;
}

// スカラー変数 name を hl にロード（st==3 ポインタ / st==4 int は 16bit、
// その他は 8bit ゼロ拡張）
static void gen_load_scalar(const char *name, int st, FILE *out) {
    if (st == 5) {
        g32_load_var(name, out);            // de:hl。tizix.c 対応形は g32_load_var 側
    } else if (st == 3 || st == 4) {
        fprintf(out, "    ld hl, (var_%s)\n", name);
    } else {
        fprintf(out, "    ld a, (var_%s)\n", name);
        fprintf(out, "    ld l, a\n");
        fprintf(out, "    ld h, #0\n");
    }
}

// hl(long は de:hl) をスカラー変数 name にストア（st==3/4 は 16bit / 5 は 32bit / 他 8bit）
static void gen_store_scalar(const char *name, int st, FILE *out) {
    if (st == 5) {
        g32_store_var(name, out);           // de:hl -> var(4B)。tizix.c 対応形
    } else if (st == 3 || st == 4) {
        fprintf(out, "    ld (var_%s), hl\n", name);
    } else {
        fprintf(out, "    ld a, l\n");
        fprintf(out, "    ld (var_%s), a\n", name);
    }
}

// 配列添字ノード idx の「要素アドレス」を hl に置く（読み書き共通）。
// idx->array_size に要素バイトサイズがベイクされていればそれを、無ければ
// char=1 / int=2 を使う。base がポインタ変数ならその値、配列なら先頭アドレス。
static int gen_elem_addr(Node *idx, FILE *out) {
    Node *b = idx->left;
    int esz = idx->array_size;
    if (b->type == NODE_IDENTIFIER) {
        int bt = find_sym_type(root_node, b->value);
        if (esz <= 0) { esz = find_decl_esz(root_node, b->value); if (esz <= 0) esz = 1; }
        if (bt == 3) fprintf(out, "    ld hl, (var_%s)\n", b->value);
        else         fprintf(out, "    ld hl, #var_%s\n", b->value);
    } else {
        if (esz <= 0) {
            esz = 2;
            // b が `ident[k]`（ident は char** / char*[]）なら、その結果は char*。
            // よって外側の添字は 1 バイト刻み（argv[i][j] の j が該当）。
            if (b->type == NODE_INDEX && b->left && b->left->type == NODE_IDENTIFIER
                && find_ptr_is_ptrarr(root_node, b->left->value))
                esz = 1;
        }
        generate_asm(b, out);              // base アドレス/ポインタ値 -> hl
    }
    fprintf(out, "    push hl\n");
    generate_asm(idx->right, out);          // 添字 -> hl
    if (esz == 2) fprintf(out, "    add hl, hl\n");
    else if (esz == 4) fprintf(out, "    add hl, hl\n    add hl, hl\n");
    else if (esz == 8) fprintf(out, "    add hl, hl\n    add hl, hl\n    add hl, hl\n");
    else if (esz != 1) {
        fprintf(out, "    push hl\n    ld hl, #%d\n    push hl\n    call _mul\n    pop af\n    pop af\n", esz);
    }
    fprintf(out, "    pop de\n    add hl, de\n");  // hl = 要素アドレス
    return esz;
}

// ++/-- の一般形（a[i]++ / p->m++ / v.m++）用: target のアドレスを hl へ置き、
// 要素バイトサイズ（1 or 2）を返す。
static int lval_addr(Node *t, FILE *out) {
    if (t->type == NODE_INDEX) {
        return gen_elem_addr(t, out);
    } else if (t->type == NODE_MEMBER) {
        int save = t->base_type;
        t->base_type = 2;              // アドレスモード
        generate_asm(t, out);
        t->base_type = save;
        return 2;
    } else if (t->type == NODE_IDENTIFIER) {
        int st = find_sym_type(root_node, t->value);
        fprintf(out, "    ld hl, #var_%s\n", t->value);
        return (st == 1) ? 1 : 2;
    }
    fprintf(out, "    ld hl, #0\n");
    return 2;
}

static void emit_string_content(const char *s, FILE *out) {
    fprintf(out, "    .ascii \"");
    while (*s) {
        if (*s == '\n') {
            fprintf(out, "\"\n    .db 10\n    .ascii \"");
        } else if (*s == '\r') {
            fprintf(out, "\"\n    .db 13\n    .ascii \"");
        } else if (*s == '\\') {
            fprintf(out, "\\\\");
        } else if (*s == '"') {
            fprintf(out, "\\\"");
        } else {
            fputc(*s, out);
        }
        s++;
    }
    fprintf(out, "\"\n    .db 0\n");
}

// 変数名の重複出力を防ぐ（関数をまたいで i / p / n 等が再利用されるため。
// tzcc はスコープ無しの「全変数グローバル」モデルなので同名は同一記憶域を共有する）。
static char *g_emitted[1024];
static int   g_emitted_n = 0;
static int emit_seen(const char *name) {
    for (int i = 0; i < g_emitted_n; i++)
        if (strcmp(g_emitted[i], name) == 0) return 1;
    if (g_emitted_n < 1024) g_emitted[g_emitted_n++] = strdup(name);
    return 0;
}

/* ================= 関数間の同名ローカル検出 (tizix #31) =================
 * tzcc はスコープを持たず、全ローカルを var_<name> の静的領域へ置く。よって
 * **別々の関数が同じ名前のローカルを宣言すると同一の記憶域を共有する**。
 * A が B を呼ぶと A 側の値が黙って壊れる。コンパイルも通るし警告も出ないので
 * 発見が難しい。
 *   実例: user/date.c の p2() と main() が両方 `d` を持っており、
 *   date が 1970-01-0**0** を出した(p2(m) の呼び出しが main の d を潰した)。
 * ここで検出して知らせる。--tizix-user では致命的エラーにする
 * (tizix のコマンドは全部この経路で、実機でしか症状が出ないため)。
 */
static char *g_dl_name[2048];
static char *g_dl_fn[2048];
static int   g_dl_n = 0;
static int   g_dl_bad = 0;
int g_dupcheck_fatal = 0;      /* main.c が --tizix-user のとき 1 にする */

static void dl_add(const char *name, const char *fn) {
    int i;
    for (i = 0; i < g_dl_n; i++) {
        if (strcmp(g_dl_name[i], name) == 0) {
            if (strcmp(g_dl_fn[i], fn) != 0) {
                fprintf(stderr,
                    "tzcc: %s: '%s' is also declared in '%s'\n"
                    "      tzcc has no scopes: both share one storage (var_%s), so a call\n"
                    "      between these two functions silently corrupts the other's value.\n"
                    "      Rename one of them.\n",
                    fn, name, g_dl_fn[i], name);
                g_dl_bad = 1;
            }
            return;
        }
    }
    if (g_dl_n < 2048) {
        g_dl_name[g_dl_n] = strdup(name);
        g_dl_fn[g_dl_n] = strdup(fn);
        g_dl_n++;
    }
}

/* 宣言を拾う。NODE_FUNC の中には入らない(トップレベルの走査側が回す)。 */
static void dl_walk(Node *n, const char *fn) {
    if (!n) return;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_PTR_DECL ||
         n->type == NODE_ARRAY_DECL) && n->value)
        dl_add(n->value, fn);
    if (n->type != NODE_FUNC) {
        dl_walk(n->left, fn);
        dl_walk(n->right, fn);
        dl_walk(n->third, fn);
        dl_walk(n->fourth, fn);
    }
    dl_walk(n->next, fn);
}

static void check_dup_locals(Node *list) {
    Node *p;
    for (p = list; p; p = p->next) {
        Node *save = p->next;
        p->next = NULL;
        if (p->type == NODE_FUNC) {
            const char *fn = p->value ? p->value : "?";
            if (p->right) { dl_walk(p->left, fn); dl_walk(p->right, fn); }
            else          { dl_walk(p->left, fn); }
        } else {
            dl_walk(p, "(file scope)");
        }
        p->next = save;
    }
    if (g_dl_bad && g_dupcheck_fatal) {
        fprintf(stderr, "tzcc: --tizix-user: aborting due to shared-storage name clashes\n");
        exit(1);
    }
}

static void emit_data(Node *node, FILE *out) {
    if (!node) return;
    if ((node->type == NODE_VAR_DECL || node->type == NODE_PTR_DECL || node->type == NODE_ARRAY_DECL)
        && node->value && emit_seen(node->value)) {
        // 既に出力済み: この宣言のデータは飛ばす（左辺の文字列などは下で辿る）
        emit_data(node->left, out);
        emit_data(node->right, out);
        emit_data(node->third, out);
        emit_data(node->fourth, out);
        emit_data(node->next, out);
        return;
    }
    if (node->type == NODE_VAR_DECL) {
        fprintf(out, "var_%s:\n", node->value);
        if (node->base_type == 2)      fprintf(out, "    .ds 4\n");   // long
        else if (node->base_type == 1) fprintf(out, "    .dw 0\n");
        else                           fprintf(out, "    .db 0\n");
    } else if (node->type == NODE_PTR_DECL) {
        fprintf(out, "var_%s:\n", node->value);
        if (node->left && (node->left->type == NODE_STRING || node->left->type == NODE_ARG) && node->left->value) {
            fprintf(out, "    .dw str_%lu\n", (unsigned long)(uintptr_t)node->left);
        } else {
            fprintf(out, "    .dw 0\n");
        }
    } else if (node->type == NODE_ARRAY_DECL) {
        fprintf(out, "var_%s:\n", node->value);
        if (node->left && node->left->type == NODE_STRING && node->left->value) {
            // char s[] = "..."; / char s[N] = "...";
            // 文字列長+1 を確保。宣言サイズが大きければそちらに合わせる。
            size_t slen = strlen(node->left->value);
            emit_string_content(node->left->value, out);
            if (node->array_size > (int)(slen + 1)) {
                fprintf(out, "    .ds %d\n", node->array_size - (int)(slen + 1));
            }
        } else if (node->left && node->left->type == NODE_INITLIST) {
            // T arr[] = { a, b, c };  要素を .db / .dw で並べる
            int esz = (node->base_type == 2) ? 4 : (node->base_type == 1 ? 2 : 1);
            int cnt = 0;
            for (Node *e = node->left->left; e; e = e->next) {
                unsigned long v = (e->type == NODE_NUMBER) ? num_u32(e->value) : 0;
                if (esz == 1)      fprintf(out, "    .db %u\n", (unsigned)(v & 0xFFUL));
                else if (esz == 2) fprintf(out, "    .dw %u\n", (unsigned)(v & 0xFFFFUL));
                else               fprintf(out, "    .dw %u\n    .dw %u\n",
                                           (unsigned)(v & 0xFFFFUL), (unsigned)((v >> 16) & 0xFFFFUL));
                cnt++;
            }
            if (node->array_size > cnt)
                fprintf(out, "    .ds %d\n", (node->array_size - cnt) * esz);
        } else {
            int n = node->array_size > 0 ? node->array_size : 1;
            if (node->base_type == 2) n *= 4;        // long 配列
            else if (node->base_type == 1) n *= 2;   // int 配列は要素2バイト
            fprintf(out, "    .ds %d\n", n);
        }
    } else if ((node->type == NODE_STRING || node->type == NODE_ARG) && node->value) {
        fprintf(out, "str_%lu:\n", (unsigned long)(uintptr_t)node);
        emit_string_content(node->value, out);
    }

    if (node->type != NODE_ARRAY_DECL) {
        emit_data(node->left, out);
    }
    emit_data(node->right, out);
    emit_data(node->third, out);   // if の else節 / for の post式
    emit_data(node->fourth, out);  // for の body
    emit_data(node->next, out);
}

// 式 n を評価して 32bit 値を de:hl に置く（16bit 式は評価後ゼロ拡張）
static void gen_long(Node *n, FILE *out) {
    if (!n) { fprintf(out, "    ld hl, #0\n    ld de, #0\n"); return; }
    switch (n->type) {
        case NODE_NUMBER:
            g32_lit(n->value, out);
            return;
        case NODE_IDENTIFIER:
            if (find_sym_type(root_node, n->value) == 5) g32_load_var(n->value, out);
            else { generate_asm(n, out); g32_widen(out); }
            return;
        case NODE_CVT:
            if (n->base_type == 2 && n->array_size == 0) {
                if (expr_is_long(n->left)) gen_long(n->left, out);
                else { generate_asm(n->left, out); g32_widen(out); }
            } else {
                generate_asm(n->left, out); g32_widen(out);
            }
            return;
        case NODE_ADD:
        case NODE_SUB:
            gen_long(n->left, out);
            g32_push(out);
            gen_long(n->right, out);
            g32_addsub(n->type == NODE_ADD, out);
            return;
        case NODE_TERNARY: {
            int l = label_id++;
            generate_asm(n->left, out);              // 条件 -> hl (16bit)
            fprintf(out, "    ld a, h\n    or l\n    jp z, Ltf%d\n", l);
            gen_long(n->right, out);
            fprintf(out, "    jp Lte%d\n", l);
            fprintf(out, "Ltf%d:\n", l);
            gen_long(n->third, out);
            fprintf(out, "Lte%d:\n", l);
            return;
        }
        case NODE_CALL:
            generate_asm(n, out);   // long 戻り値なら de:hl / でなければ 16bit を拡大
            if (!find_func_is_long(root_node, n->value)) g32_widen(out);
            return;
        case NODE_DEREF:
            generate_asm(n->left, out);   // アドレス -> hl
            fprintf(out,
                "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n    inc hl\n"  // de = 下位16
                "    ex de, hl\n    push hl\n    ex de, hl\n"               // [sp]=下位16, hl=addr+2
                "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n"              // de = 上位16
                "    pop hl\n");                                            // hl = 下位16
            return;
        case NODE_INDEX:
            gen_elem_addr(n, out);        // hl = 要素アドレス（esz=4 でストライド済み）
            fprintf(out,
                "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n    inc hl\n"
                "    ex de, hl\n    push hl\n    ex de, hl\n"
                "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n"
                "    pop hl\n");
            return;
        case NODE_PREINC: case NODE_PREDEC:
        case NODE_POSTINC: case NODE_POSTDEC:
            generate_asm(n, out);   // switch 側で 32bit inc/dec 済み、結果 de:hl
            return;
        case NODE_MEMBER:
            if (n->esz == 4 && n->base_type != 2) { generate_asm(n, out); return; }  // long メンバ: 4byte 済
            generate_asm(n, out);
            g32_widen(out);
            return;
        default:
            generate_asm(n, out);
            g32_widen(out);
            return;
    }
}

/* 16bit の実引数 1 個を評価して hl に置く(NODE_CALL の引数積みと CALLI の
 * 呼び先で共用。#70 で括り出した)。long 実引数は呼び出し側が gen_long で扱う。 */
static void gen_arg_hl(Node *arg, FILE *out) {
    if (arg->type == NODE_IDENTIFIER) {
        int st = find_sym_type(root_node, arg->value);
        if (is_std_stream(arg->value)) { // stdin/stdout/stderr
            fprintf(out, "    ld hl, (_%s)\n", arg->value);
        } else if (st == 3) { // Pointer
            fprintf(out, "    ld hl, (var_%s)\n", arg->value);
        } else if (st == 2) { // Array
            fprintf(out, "    ld hl, #var_%s\n", arg->value);
        } else { // スカラー (char=1byte / int=2byte)
            gen_load_scalar(arg->value, st, out);
        }
    } else if (arg->type == NODE_STRING || arg->type == NODE_ARG) {
        fprintf(out, "    ld hl, #str_%lu\n", (unsigned long)(uintptr_t)arg);
    } else if (arg->type == NODE_NUMBER) {
        fprintf(out, "    ld hl, #%s\n", arg->value); // 16bit (>255 対応)
    } else if (is_expr_node(arg->type) || arg->type == NODE_CALL) {
        // 兄弟(次の引数)を辿らないよう next を退避してから評価。
        // NODE_CALL も含める: f(g(x)) の g(x) を実際に評価する
        // (含めないと直前の hl が押されてしまう)。
        Node *saved = arg->next;
        arg->next = NULL;
        generate_asm(arg, out); // 結果は hl
        arg->next = saved;
    }
}

void generate_asm(Node *node, FILE *out) {
    if (!node) return;

    switch (node->type) {
        case NODE_ROOT:
            root_node = node;
            fprintf(out, "; Generated by TzCC\n");
            check_dup_locals(node->left);
            fprintf(out, "    .area _CODE\n\n");
            generate_asm(node->left, out);
            fprintf(out, "\n    .area _DATA\n");
            emit_data(node->left, out);
            break;

        case NODE_FUNC:
            fprintf(out, "_%s::\n", node->value);
            Node *params = NULL;
            Node *body = NULL;
            if (node->right) {
                params = node->left;
                body = node->right;
            } else {
                body = node->left;
            }

            if (params) {
                fprintf(out, "    ld ix, #0\n");
                fprintf(out, "    add ix, sp\n");
                int offset = 2;
                Node *p = params;
                while (p) {
                    if (p->type != NODE_PTR_DECL && p->base_type == 2) {
                        // long 仮引数 = 4byte(2 word)。呼び出し側も push de/push hl で 4byte 積む
                        fprintf(out, "    ld l, %d(ix)\n    ld h, %d(ix)\n", offset, offset + 1);
                        fprintf(out, "    ld e, %d(ix)\n    ld d, %d(ix)\n", offset + 2, offset + 3);
                        g32_store_var(p->value, out);
                        offset += 4;
                    } else if (p->type == NODE_PTR_DECL || p->base_type == 1) {
                        // ポインタ / int は 16bit で受け取る（引数は常に 2byte push される）
                        fprintf(out, "    ld l, %d(ix)\n", offset);
                        fprintf(out, "    ld h, %d(ix)\n", offset + 1);
                        fprintf(out, "    ld (var_%s), hl\n", p->value);
                        offset += 2;
                    } else {
                        fprintf(out, "    ld a, %d(ix)\n", offset);
                        fprintf(out, "    ld (var_%s), a\n", p->value);
                        offset += 2;
                    }
                    p = p->next;
                }
            }

            if (body) generate_asm(body, out);
            fprintf(out, "    ret\n");
            break;

        case NODE_BLOCK:
            generate_asm(node->left, out);
            break;

        case NODE_ARRAY_DECL:
            // 記憶域は emit_data が _DATA 上に確保する。実行時コードは不要。
            break;

        case NODE_VAR_DECL:
            if (node->left) {
                if (node->base_type == 2) {              // long a = <式>;
                    if (node->left->type == NODE_NUMBER) g32_lit(node->left->value, out);
                    else gen_long(node->left, out);
                    gen_store_scalar(node->value, 5, out);
                    break;
                }
                int dst = (node->base_type == 1) ? 4 : 1; // 自ノードの型
                if (node->left->type == NODE_NUMBER) {
                    if (dst == 4) {
                        fprintf(out, "    ld hl, #%s\n", node->left->value);
                        fprintf(out, "    ld (var_%s), hl\n", node->value);
                    } else {
                        fprintf(out, "    ld a, #%s\n", node->left->value);
                        fprintf(out, "    ld (var_%s), a\n", node->value);
                    }
                } else if (node->left->type == NODE_STRING && node->left->value) {
                    fprintf(out, "    ld a, #%d\n", (unsigned char)node->left->value[0]);
                    fprintf(out, "    ld (var_%s), a\n", node->value);
                } else if (node->left->type == NODE_CALL) {
                    generate_asm(node->left, out); // 戻り値は hl
                    gen_store_scalar(node->value, dst, out);
                } else if (node->left->type == NODE_IDENTIFIER) {
                    int rst = find_sym_type(root_node, node->left->value);
                    gen_load_scalar(node->left->value, rst, out);
                    gen_store_scalar(node->value, dst, out);
                } else if (is_expr_node(node->left->type)) {
                    generate_asm(node->left, out); // 結果は hl
                    gen_store_scalar(node->value, dst, out);
                }
            }
            break;

        case NODE_PTR_DECL:
            if (node->left) {
                if (node->left->type == NODE_STRING || node->left->type == NODE_ARG) {
                    fprintf(out, "    ld hl, #str_%lu\n", (unsigned long)(uintptr_t)node->left);
                    fprintf(out, "    ld (var_%s), hl\n", node->value);
                } else if (node->left->type == NODE_CALL) {
                    generate_asm(node->left, out);
                    fprintf(out, "    ld (var_%s), hl\n", node->value);
                } else if (is_expr_node(node->left->type)) {
                    // char *s = argv[i];  char *p = base + n;  等（初期化子が式）
                    generate_asm(node->left, out);            // 結果は hl
                    fprintf(out, "    ld (var_%s), hl\n", node->value);
                } else if (node->left->type == NODE_IDENTIFIER) {
                    // FILE *fp = stdout;  やポインタ変数どうしのコピー
                    if (is_std_stream(node->left->value)) {
                        fprintf(out, "    ld hl, (_%s)\n", node->left->value);
                    } else {
                        int rst = find_sym_type(root_node, node->left->value);
                        if (rst == 2) {
                            fprintf(out, "    ld hl, #var_%s\n", node->left->value);
                        } else {
                            fprintf(out, "    ld hl, (var_%s)\n", node->left->value);
                        }
                    }
                    fprintf(out, "    ld (var_%s), hl\n", node->value);
                }
            }
            break;

        case NODE_ASSIGN:
            if (node->left) {
                int st = find_sym_type(root_node, node->value);
                if (st == 5) {                          // long 変数への代入
                    if (node->left->type == NODE_NUMBER) g32_lit(node->left->value, out);
                    else gen_long(node->left, out);     // 16bit 式なら中で拡大
                    gen_store_scalar(node->value, 5, out);
                    break;
                }
                if (node->left->type == NODE_CALL) {
                    generate_asm(node->left, out);
                    gen_store_scalar(node->value, st, out);
                } else if (is_expr_node(node->left->type)) {
                    generate_asm(node->left, out); // 結果は hl
                    gen_store_scalar(node->value, st, out);
                } else if (st == 3) { // Pointer variable
                    if (node->left->type == NODE_STRING || node->left->type == NODE_ARG) {
                        fprintf(out, "    ld hl, #str_%lu\n", (unsigned long)(uintptr_t)node->left);
                        fprintf(out, "    ld (var_%s), hl\n", node->value);
                    } else if (node->left->type == NODE_IDENTIFIER) {
                        if (is_std_stream(node->left->value)) {
                            fprintf(out, "    ld hl, (_%s)\n", node->left->value);
                        } else {
                            int rst = find_sym_type(root_node, node->left->value);
                            if (rst == 2) {
                                fprintf(out, "    ld hl, #var_%s\n", node->left->value);
                            } else {
                                fprintf(out, "    ld hl, (var_%s)\n", node->left->value);
                            }
                        }
                        fprintf(out, "    ld (var_%s), hl\n", node->value);
                    }
                } else {
                    if (node->left->type == NODE_NUMBER) {
                        if (st == 4) {
                            fprintf(out, "    ld hl, #%s\n", node->left->value);
                            fprintf(out, "    ld (var_%s), hl\n", node->value);
                        } else {
                            fprintf(out, "    ld a, #%s\n", node->left->value);
                            fprintf(out, "    ld (var_%s), a\n", node->value);
                        }
                    } else if (node->left->type == NODE_IDENTIFIER) {
                        int rst = find_sym_type(root_node, node->left->value);
                        gen_load_scalar(node->left->value, rst, out);
                        gen_store_scalar(node->value, st, out);
                    } else if (node->left->type == NODE_STRING && node->left->value) {
                        if (st == 4) {
                            fprintf(out, "    ld hl, #%d\n", (unsigned char)node->left->value[0]);
                            fprintf(out, "    ld (var_%s), hl\n", node->value);
                        } else {
                            fprintf(out, "    ld a, #%d\n", (unsigned char)node->left->value[0]);
                            fprintf(out, "    ld (var_%s), a\n", node->value);
                        }
                    }
                }
            }
            break;

        case NODE_CALL: {
            /* #70: 組み込みマクロ(ANSI の関数ポインタではない。型検査は無い)。
             *   FNADDR(name)          関数 name の **絶対番地** を hl に。
             *                         `ld hl, #_name` を吐くだけで、--tizix-user では
             *                         tizix.c が +IY するので実行時の絶対番地になる。
             *                         (リンク時オフセットを変数へ持たせると「配列アドレスの
             *                         代入は +IY されない」穴と同じ形になるので絶対番地で返す)
             *   CALLI(addr, a, b, ..) 絶対番地 addr を a, b, .. を引数に呼ぶ。戻り値は hl。
             *                         引数は通常の呼び出しと同じ積み方。呼び先は **引数の後に**
             *                         評価する(hl は引数の評価にも使うので先に入れると潰れる)。
             *                         ___sdcc_call_hl(jp (hl))経由。tizix.c はこれを
             *                         再配置せず素通しし、呼び出し後に IX を張り直す。 */
            int calli = node->value && strcmp(node->value, "CALLI") == 0;
            if (node->value && strcmp(node->value, "FNADDR") == 0) {
                Node *f = node->left;
                if (!f || f->type != NODE_IDENTIFIER || f->next) {
                    fprintf(stderr, "tzcc: FNADDR には関数名を 1 個だけ渡す\n");
                    exit(1);
                }
                fprintf(out, "    ld hl, #_%s\n", f->value);
                break;
            }
            if (calli && !node->left) {
                fprintf(stderr, "tzcc: CALLI には呼び先の番地が要る\n");
                exit(1);
            }
            if (node->left) {
                int arg_count = 0;
                Node *args[16];
                Node *a = node->left;
                while (a && arg_count < 16) {
                    args[arg_count++] = a;
                    a = a->next;
                }
                int pushed_words = 0;
                int first = calli ? 1 : 0;        // CALLI: args[0] は呼び先(積まない)
                for (int i = arg_count - 1; i >= first; i--) {
                    Node *arg = args[i];
                    if (expr_is_long(arg)) {          // long 実引数 = 4byte push
                        Node *saved = arg->next; arg->next = NULL;
                        gen_long(arg, out);          // de:hl
                        arg->next = saved;
                        fprintf(out, "    push de\n    push hl\n");
                        pushed_words += 2;
                        continue;
                    }
                    gen_arg_hl(arg, out);
                    fprintf(out, "    push hl\n");
                    pushed_words += 1;
                }
                if (calli) {
                    gen_arg_hl(args[0], out);     // 呼び先 -> hl(引数の後)
                    fprintf(out, "    call ___sdcc_call_hl\n");
                } else {
                    fprintf(out, "    call _%s\n", node->value);
                }
                for (int i = 0; i < pushed_words; i++) {
                    fprintf(out, "    pop af\n");
                }
            } else {
                fprintf(out, "    call _%s\n", node->value);
            }
            break;
        }

        case NODE_RETURN:
            if (node->left) {
                if (expr_is_long(node->left)) {
                    gen_long(node->left, out);          // long 戻り値 -> de:hl
                } else if (node->left->type == NODE_NUMBER) {
                    fprintf(out, "    ld hl, #%s\n", node->left->value); // 16bit (>255 対応)
                } else if (node->left->type == NODE_STRING || node->left->type == NODE_ARG) {
                    fprintf(out, "    ld hl, #str_%lu\n", (unsigned long)(uintptr_t)node->left);
                } else if (node->left->type == NODE_IDENTIFIER) {
                    int st = find_sym_type(root_node, node->left->value);
                    if (st == 3) {
                        fprintf(out, "    ld hl, (var_%s)\n", node->left->value);
                    } else if (st == 2) {
                        fprintf(out, "    ld hl, #var_%s\n", node->left->value);
                    } else {
                        gen_load_scalar(node->left->value, st, out);
                    }
                } else if (node->left->type == NODE_CALL) {
                    // ★`return f(x);`。ここが無いと **文が丸ごと落ちて**
                    //   呼び出し側は直前に hl に残っていた値を受け取る。
                    //   tizix の vi で line_prev() が「前の行頭」ではなく
                    //   「いまの行頭」を返し、k / 上カーソルが無反応になった。
                    generate_asm(node->left, out); // 戻り値は hl
                } else if (is_expr_node(node->left->type)) {
                    generate_asm(node->left, out); // 結果は hl
                } else {
                    // 黙って捨てないための番人。生成できない形は必ず落とす。
                    fprintf(stderr, "tzcc: return の式を生成できません"
                                    " (node type %d)\n", (int)node->left->type);
                    exit(1);
                }
            }
            // 関数途中の return も確実に戻す。tzcc はスタックフレームを積まない
            // （引数・局所はグローバル var_*）ので、文の位置では SP=入口 SP。
            // よって bare ret で実戻り番地へ返れる（--tizix-user の tizix.c 変換も
            // ret は素通し）。末尾 return は NODE_FUNC 末尾の ret と二重になるが無害。
            fprintf(out, "    ret\n");
            break;

        case NODE_NUMBER:
            if (expr_is_long(node)) g32_lit(node->value, out);   // de:hl
            else fprintf(out, "    ld hl, #%s\n", node->value);
            break;

        case NODE_CVT:
            if (node->array_size == 1) {
                generate_asm(node->left, out);          // ポインタキャスト: 値素通し(16bit)
            } else if (node->base_type == 2) {
                gen_long(node->left, out);              // long へ拡大 -> de:hl
            } else {
                generate_asm(node->left, out);          // 16bit へ縮小: hl を採用
            }
            break;

        case NODE_IDENTIFIER: {
            int st = find_sym_type(root_node, node->value);
            if (is_std_stream(node->value)) {
                fprintf(out, "    ld hl, (_%s)\n", node->value);
            } else if (st == 3) {          // ポインタ
                fprintf(out, "    ld hl, (var_%s)\n", node->value);
            } else if (st == 2) {          // 配列 → 先頭アドレス
                fprintf(out, "    ld hl, #var_%s\n", node->value);
            } else {                       // スカラー（char=1byte / int=2byte）
                gen_load_scalar(node->value, st, out);
            }
            break;
        }

        case NODE_STRING:
            fprintf(out, "    ld hl, #str_%lu\n", (unsigned long)(uintptr_t)node);
            break;

        // ---- 配列添字 / ポインタ ----
        case NODE_INDEX: {
            if (node->base_type != 2 && expr_is_long(node)) { gen_long(node, out); break; }  // long 配列要素 -> de:hl
            int esz = gen_elem_addr(node, out);        // hl = 要素アドレス
            if (node->base_type == 2) break;           // 多次元途中: アドレスのまま返す
            if (esz >= 2) fprintf(out, "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n");
            else          fprintf(out, "    ld a, (hl)\n    ld l, a\n    ld h, #0\n");
            break;
        }

        case NODE_STORE_INDEX: {
            // node->left=識別子, node->right=添字, node->third=値
            if (node->left && node->left->type == NODE_IDENTIFIER
                && find_decl_esz(root_node, node->left->value) == 4) {   // long 配列要素への代入
                gen_long(node->third, out);            // 値 -> de:hl
                fprintf(out, "    push de\n    push hl\n");
                gen_elem_addr(node, out);              // hl = 要素アドレス
                fprintf(out,
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n    inc hl\n"
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n");
                break;
            }
            generate_asm(node->third, out);            // 値 -> hl
            fprintf(out, "    push hl\n");
            int esz = gen_elem_addr(node, out);        // hl = 要素アドレス
            fprintf(out, "    pop de\n");              // de = 値
            if (esz >= 2) fprintf(out, "    ld (hl), e\n    inc hl\n    ld (hl), d\n");
            else          fprintf(out, "    ld (hl), e\n");
            break;
        }

        case NODE_DEREF:
            if (expr_is_long(node)) { gen_long(node, out); break; }   // *(long*)p -> de:hl
            generate_asm(node->left, out);   // ポインタ値 -> hl
            fprintf(out, "    ld a, (hl)\n    ld l, a\n    ld h, #0\n");
            break;

        case NODE_STORE_DEREF:
            if (node->left && node->left->type == NODE_IDENTIFIER
                && find_ptr_is_long(root_node, node->left->value)) {   // *p = <long>  (p は long*)
                gen_long(node->right, out);                // 値 -> de:hl
                fprintf(out, "    push de\n    push hl\n");
                generate_asm(node->left, out);             // ポインタ値 -> hl
                fprintf(out,
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n    inc hl\n"
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n");
                break;
            }
            generate_asm(node->right, out);  // 値 -> hl
            fprintf(out, "    push hl\n");
            generate_asm(node->left, out);   // ポインタ値 -> hl
            fprintf(out, "    pop de\n");     // de = 値
            fprintf(out, "    ld (hl), e\n");
            break;

        case NODE_ADDR:
            if (node->left && node->left->type == NODE_IDENTIFIER) {
                fprintf(out, "    ld hl, #var_%s\n", node->left->value);
            } else if (node->left && node->left->type == NODE_INDEX) {
                gen_elem_addr(node->left, out);   // &a[i] = 要素アドレス
            } else if (node->left && node->left->type == NODE_MEMBER) {
                int save = node->left->base_type;
                node->left->base_type = 2;        // アドレスモード（ロードしない）
                generate_asm(node->left, out);
                node->left->base_type = save;
            } else {
                fprintf(out, "    ld hl, #0\n");
            }
            break;

        // ---- struct メンバアクセス（連鎖対応） ----
        // node->left = base（IDENTIFIER または内側の NODE_MEMBER）
        // node->value = "->" or "."  /  node->array_size = byte オフセット
        case NODE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) fprintf(out, "    ld hl, (var_%s)\n", b->value);  // ポインタ値
                else       fprintf(out, "    ld hl, #var_%s\n", b->value);   // 構造体先頭アドレス
            } else {
                generate_asm(b, out);   // 連鎖: 内側メンバの値(=ポインタ) が hl
            }
            if (node->array_size)
                fprintf(out, "    ld de, #%d\n    add hl, de\n", node->array_size);
            if (node->base_type == 2) break;   // 配列メンバ: アドレスを返す
            if (node->esz == 1)
                fprintf(out, "    ld a, (hl)\n    ld l, a\n    ld h, #0\n");            // char メンバ
            else if (node->esz == 4)
                fprintf(out,
                    "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n    inc hl\n"
                    "    ex de, hl\n    push hl\n    ex de, hl\n"
                    "    ld e, (hl)\n    inc hl\n    ld d, (hl)\n"
                    "    pop hl\n");                                                   // long メンバ -> de:hl
            else
                fprintf(out, "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n"); // int/ptr メンバ
            break;
        }

        case NODE_STORE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            if (node->esz == 4) {
                gen_long(node->right, out);       // 値 -> de:hl
                fprintf(out, "    push de\n    push hl\n");
            } else {
                generate_asm(node->right, out);   // 値 -> hl
                fprintf(out, "    push hl\n");
            }
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) fprintf(out, "    ld hl, (var_%s)\n", b->value);
                else       fprintf(out, "    ld hl, #var_%s\n", b->value);
            } else {
                generate_asm(b, out);
            }
            if (node->array_size)
                fprintf(out, "    ld de, #%d\n    add hl, de\n", node->array_size);
            if (node->esz == 1) {
                fprintf(out, "    pop de\n    ld (hl), e\n");                          // char メンバ
            } else if (node->esz == 4) {
                fprintf(out,
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n    inc hl\n"
                    "    pop bc\n    ld (hl), c\n    inc hl\n    ld (hl), b\n");        // long メンバ
            } else {
                fprintf(out, "    pop de\n    ld (hl), e\n    inc hl\n    ld (hl), d\n"); // int/ptr メンバ
            }
            break;
        }

        case NODE_PREINC:
        case NODE_PREDEC: {
            if (node->value) {
                int st = find_sym_type(root_node, node->value);
                if (st == 5) {                          // 32bit ++/--
                    int l = label_id++;
                    g32_load_var(node->value, out);
                    if (node->type == NODE_PREINC)
                        fprintf(out, "    inc hl\n    ld a, h\n    or l\n    jr nz, Lli%d\n    inc de\nLli%d:\n", l, l);
                    else
                        fprintf(out, "    ld a, h\n    or l\n    jr nz, Lli%d\n    dec de\nLli%d:\n    dec hl\n", l, l);
                    g32_store_var(node->value, out);    // 評価結果 = 新しい値 (de:hl)
                    break;
                }
                gen_load_scalar(node->value, st, out);
                fprintf(out, node->type == NODE_PREINC ? "    inc hl\n" : "    dec hl\n");
                gen_store_scalar(node->value, st, out);
                // 評価結果 = 新しい値（hl のまま）
            } else {
                int esz = lval_addr(node->left, out);      // hl = アドレス
                fprintf(out, "    push hl\n");
                if (esz >= 2) fprintf(out, "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n");
                else          fprintf(out, "    ld a, (hl)\n    ld l, a\n    ld h, #0\n");
                fprintf(out, node->type == NODE_PREINC ? "    inc hl\n" : "    dec hl\n");
                fprintf(out, "    ld b, h\n    ld c, l\n");   // bc = 新しい値
                fprintf(out, "    pop de\n");                  // de = アドレス
                if (esz >= 2) fprintf(out, "    ld a, c\n    ld (de), a\n    inc de\n    ld a, b\n    ld (de), a\n");
                else          fprintf(out, "    ld a, c\n    ld (de), a\n");
                fprintf(out, "    ld h, b\n    ld l, c\n");    // 評価結果 = 新しい値
            }
            break;
        }

        case NODE_POSTINC:
        case NODE_POSTDEC: {
            if (node->value) {
                int st = find_sym_type(root_node, node->value);
                if (st == 5) {                          // 32bit ++/--
                    int l = label_id++;
                    g32_load_var(node->value, out);
                    g32_push(out);                       // 旧値退避
                    if (node->type == NODE_POSTINC)
                        fprintf(out, "    inc hl\n    ld a, h\n    or l\n    jr nz, Lli%d\n    inc de\nLli%d:\n", l, l);
                    else
                        fprintf(out, "    ld a, h\n    or l\n    jr nz, Lli%d\n    dec de\nLli%d:\n    dec hl\n", l, l);
                    g32_store_var(node->value, out);
                    fprintf(out, "    pop hl\n    pop de\n");   // 評価結果 = 旧値
                    break;
                }
                gen_load_scalar(node->value, st, out);       // hl = 旧値
                fprintf(out, "    push hl\n");
                fprintf(out, node->type == NODE_POSTINC ? "    inc hl\n" : "    dec hl\n");
                gen_store_scalar(node->value, st, out);
                fprintf(out, "    pop hl\n");                 // 評価結果 = 旧値
            } else {
                int esz = lval_addr(node->left, out);      // hl = アドレス
                fprintf(out, "    push hl\n");
                if (esz >= 2) fprintf(out, "    ld a, (hl)\n    inc hl\n    ld h, (hl)\n    ld l, a\n");
                else          fprintf(out, "    ld a, (hl)\n    ld l, a\n    ld h, #0\n");
                fprintf(out, "    push hl\n");                 // 旧値も退避
                fprintf(out, node->type == NODE_POSTINC ? "    inc hl\n" : "    dec hl\n");
                fprintf(out, "    ld b, h\n    ld c, l\n");    // bc = 新しい値
                fprintf(out, "    pop hl\n");                   // hl = 旧値（評価結果）
                fprintf(out, "    pop de\n");                   // de = アドレス
                if (esz >= 2) fprintf(out, "    ld a, c\n    ld (de), a\n    inc de\n    ld a, b\n    ld (de), a\n");
                else          fprintf(out, "    ld a, c\n    ld (de), a\n");
            }
            break;
        }

        case NODE_NEG:
            generate_asm(node->left, out);
            fprintf(out, "    ex de, hl\n");
            fprintf(out, "    ld hl, #0\n");
            fprintf(out, "    or a\n");
            fprintf(out, "    sbc hl, de\n");
            break;

        case NODE_AND: {
            int l = label_id++;
            generate_asm(node->left, out);
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Land%d\n", l);
            generate_asm(node->right, out);
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Land%d\n", l);
            fprintf(out, "    ld hl, #1\n");
            fprintf(out, "    jp Lae%d\n", l);
            fprintf(out, "Land%d:\n", l);
            fprintf(out, "    ld hl, #0\n");
            fprintf(out, "Lae%d:\n", l);
            break;
        }

        case NODE_OR: {
            int l = label_id++;
            generate_asm(node->left, out);
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Lor%d\n", l);   // 左が偽 → 右を評価
            fprintf(out, "    ld hl, #1\n");
            fprintf(out, "    jp Loe%d\n", l);
            fprintf(out, "Lor%d:\n", l);
            generate_asm(node->right, out);
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Lof%d\n", l);
            fprintf(out, "    ld hl, #1\n");
            fprintf(out, "    jp Loe%d\n", l);
            fprintf(out, "Lof%d:\n", l);
            fprintf(out, "    ld hl, #0\n");
            fprintf(out, "Loe%d:\n", l);
            break;
        }

        case NODE_NOT: {
            int l = label_id++;
            generate_asm(node->left, out);
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    ld hl, #1\n");
            fprintf(out, "    jr z, Lnot%d\n", l);
            fprintf(out, "    ld hl, #0\n");
            fprintf(out, "Lnot%d:\n", l);
            break;
        }

        case NODE_EQ:
        case NODE_NE:
        case NODE_LT:
        case NODE_GT:
        case NODE_LE:
        case NODE_GE: {
            int l = label_id++;
            // x <op> 0 / 0 <op> x は符号付きの意図(EOF センチネル `while (n >= 0)` 等)。
            // tzcc は符号を追わないため通常経路は unsigned 比較になり `-1 >= 0` が真に
            // なってしまう。0 との大小比較は h の bit7(符号ビット)で判定する。
            {
                Node *L = node->left, *R = node->right;
                int lz = L && L->type == NODE_NUMBER && num_u32(L->value) == 0;
                int rz = R && R->type == NODE_NUMBER && num_u32(R->value) == 0;
                if ((node->type==NODE_LT||node->type==NODE_GT||node->type==NODE_LE||node->type==NODE_GE)
                    && (lz ^ rz) && !expr_is_long(lz ? R : L)) {
                    Node *x = rz ? L : R;
                    NodeType e = node->type;
                    if (lz) e = (e==NODE_LT)?NODE_GT : (e==NODE_GT)?NODE_LT
                              : (e==NODE_LE)?NODE_GE : NODE_LE;   // 0 <op> x → x <op'> 0
                    generate_asm(x, out);                          // x -> hl
                    if (e == NODE_LT)
                        fprintf(out, "    ld a, h\n    and #0x80\n    ld hl, #0\n    jr z, Lcmp%d\n    ld hl, #1\nLcmp%d:\n", l, l);
                    else if (e == NODE_GE)
                        fprintf(out, "    ld a, h\n    and #0x80\n    ld hl, #1\n    jr z, Lcmp%d\n    ld hl, #0\nLcmp%d:\n", l, l);
                    else if (e == NODE_GT)   // x > 0 = (x>=0) && (x!=0)
                        fprintf(out, "    ld a, l\n    or h\n    jr z, Lcz%d\n    ld a, h\n    and #0x80\n    jr nz, Lcz%d\n    ld hl, #1\n    jr Lcmp%d\nLcz%d:\n    ld hl, #0\nLcmp%d:\n", l, l, l, l, l);
                    else                     // x <= 0 = (x<0) || (x==0)
                        fprintf(out, "    ld a, l\n    or h\n    jr z, Lco%d\n    ld a, h\n    and #0x80\n    jr nz, Lco%d\n    ld hl, #0\n    jr Lcmp%d\nLco%d:\n    ld hl, #1\nLcmp%d:\n", l, l, l, l, l);
                    break;
                }
            }
            if (expr_is_long(node->left) || expr_is_long(node->right)) {
                // 32bit 比較。GT/LE は左右を入れ替えて LT/GE と同型に。
                int swap = (node->type == NODE_GT || node->type == NODE_LE);
                Node *a = swap ? node->right : node->left;
                Node *b = swap ? node->left  : node->right;
                gen_long(a, out);
                g32_push(out);
                gen_long(b, out);
                g32_addsub(0, out);   // de:hl = a - b, CY = borrow(=unsigned a<b)
                if (node->type == NODE_EQ || node->type == NODE_NE) {
                    fprintf(out, "    ld a, l\n    or h\n    or e\n    or d\n");
                    if (node->type == NODE_EQ)
                        fprintf(out, "    ld hl, #1\n    jr z, Lcmp%d\n    ld hl, #0\nLcmp%d:\n", l, l);
                    else
                        fprintf(out, "    ld hl, #0\n    jr z, Lcmp%d\n    ld hl, #1\nLcmp%d:\n", l, l);
                } else if (node->type == NODE_LT || node->type == NODE_GT) {
                    fprintf(out, "    ld hl, #1\n    jr c, Lcmp%d\n    ld hl, #0\nLcmp%d:\n", l, l);
                } else { // GE / LE
                    fprintf(out, "    ld hl, #0\n    jr c, Lcmp%d\n    ld hl, #1\nLcmp%d:\n", l, l);
                }
                break;
            }
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    pop de\n");    // de = 左辺, hl = 右辺
            if (node->type == NODE_EQ || node->type == NODE_NE ||
                node->type == NODE_LT || node->type == NODE_GE) {
                // 左辺 - 右辺 が必要なので hl=左辺, de=右辺 に入れ替える
                fprintf(out, "    ex de, hl\n");
            }
            fprintf(out, "    or a\n");
            fprintf(out, "    sbc hl, de\n");
            switch (node->type) {
                case NODE_EQ:  // 等しい → Z
                    fprintf(out, "    ld hl, #1\n    jr z, Lcmp%d\n    ld hl, #0\nLcmp%d:\n", l, l);
                    break;
                case NODE_NE:  // 等しくない
                    fprintf(out, "    ld hl, #0\n    jr z, Lcmp%d\n    ld hl, #1\nLcmp%d:\n", l, l);
                    break;
                case NODE_LT:  // 左 < 右 (unsigned) → CY
                case NODE_GT:  // 右 - 左 で CY なら 左 > 右
                    fprintf(out, "    ld hl, #1\n    jr c, Lcmp%d\n    ld hl, #0\nLcmp%d:\n", l, l);
                    break;
                case NODE_GE:  // 左 >= 右 → CYなし
                case NODE_LE:  // 右 - 左 で CYなし なら 左 <= 右
                    fprintf(out, "    ld hl, #0\n    jr c, Lcmp%d\n    ld hl, #1\nLcmp%d:\n", l, l);
                    break;
                default: break;
            }
            break;
        }

        case NODE_IF: {
            int l = label_id++;
            generate_asm(node->left, out);   // 条件 -> hl
            fprintf(out, "    ld a, h\n    or l\n");
            if (node->third) {
                fprintf(out, "    jp z, Lelse%d\n", l);
                if (node->right) generate_asm(node->right, out);
                fprintf(out, "    jp Lend%d\n", l);
                fprintf(out, "Lelse%d:\n", l);
                generate_asm(node->third, out);
                fprintf(out, "Lend%d:\n", l);
            } else {
                fprintf(out, "    jp z, Lend%d\n", l);
                if (node->right) generate_asm(node->right, out);
                fprintf(out, "Lend%d:\n", l);
            }
            break;
        }

        case NODE_WHILE: {
            int l = label_id++;
            g_cont[g_cont_sp++] = l;
            g_brk[g_brk_sp++] = l;
            fprintf(out, "Lcont%d:\n", l);
            generate_asm(node->left, out);   // 条件 -> hl
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Lbrk%d\n", l);
            if (node->right) generate_asm(node->right, out);
            fprintf(out, "    jp Lcont%d\n", l);
            fprintf(out, "Lbrk%d:\n", l);
            g_cont_sp--; g_brk_sp--;
            break;
        }

        case NODE_FOR: {
            int l = label_id++;
            g_cont[g_cont_sp++] = l;
            g_brk[g_brk_sp++] = l;
            if (node->left) generate_asm(node->left, out);   // init
            fprintf(out, "Lbeg%d:\n", l);
            if (node->right) {                               // cond
                generate_asm(node->right, out);
                fprintf(out, "    ld a, h\n    or l\n");
                fprintf(out, "    jp z, Lbrk%d\n", l);
            }
            if (node->fourth) generate_asm(node->fourth, out); // body
            fprintf(out, "Lcont%d:\n", l);                     // continue → post へ
            if (node->third) generate_asm(node->third, out);   // post
            fprintf(out, "    jp Lbeg%d\n", l);
            fprintf(out, "Lbrk%d:\n", l);
            g_cont_sp--; g_brk_sp--;
            break;
        }

        case NODE_BREAK:
            if (g_brk_sp > 0) fprintf(out, "    jp Lbrk%d\n", g_brk[g_brk_sp - 1]);
            break;
        case NODE_CONTINUE:
            if (g_cont_sp > 0) fprintf(out, "    jp Lcont%d\n", g_cont[g_cont_sp - 1]);
            break;

        case NODE_CASE:
            fprintf(out, "Lcase%d_%d:\n", node->array_size, node->base_type);
            break;
        case NODE_DEFAULT:
            fprintf(out, "Ldef%d:\n", node->array_size);
            break;

        case NODE_SWITCH: {
            int l = label_id++;
            g_brk[g_brk_sp++] = l;
            generate_asm(node->left, out);   // 式 -> hl
            fprintf(out, "    ld a, l\n");
            Node *b = node->right ? node->right->left : NULL;
            int has_def = 0;
            int ci = 0;
            // dispatch
            for (Node *p = b; p; p = p->next) {
                if (p->type == NODE_CASE && p->value) {
                    p->array_size = l; p->base_type = ci;
                    fprintf(out, "    cp #%s\n    jp z, Lcase%d_%d\n", p->value, l, ci);
                    ci++;
                } else if (p->type == NODE_DEFAULT) {
                    p->array_size = l;
                    has_def = 1;
                }
            }
            fprintf(out, has_def ? "    jp Ldef%d\n" : "    jp Lbrk%d\n", l);
            // body（NODE_CASE/DEFAULT はラベルを出し、他は通常展開。fallthrough する）
            for (Node *p = b; p; p = p->next) {
                Node *save = p->next;
                p->next = NULL;
                generate_asm(p, out);
                p->next = save;
            }
            fprintf(out, "Lbrk%d:\n", l);
            g_brk_sp--;
            break;
        }

        case NODE_TERNARY: {
            int l = label_id++;
            generate_asm(node->left, out);   // 条件 -> hl
            fprintf(out, "    ld a, h\n    or l\n");
            fprintf(out, "    jp z, Ltf%d\n", l);
            generate_asm(node->right, out);
            fprintf(out, "    jp Lte%d\n", l);
            fprintf(out, "Ltf%d:\n", l);
            generate_asm(node->third, out);
            fprintf(out, "Lte%d:\n", l);
            break;
        }

        case NODE_ADD:
        case NODE_SUB:
            if (expr_is_long(node)) { gen_long(node, out); break; }   // 32bit 加減
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    pop de\n");
            if (node->type == NODE_ADD) {
                fprintf(out, "    add hl, de\n");
            } else {
                // hl = de - hl
                fprintf(out, "    ex de, hl\n");
                fprintf(out, "    or a\n");
                fprintf(out, "    sbc hl, de\n");
            }
            break;

        case NODE_MUL:
        case NODE_DIV:
            // 引数はスタック渡し（4(sp)=左, 2(sp)=右）。HL レジスタ渡しは
            // --tizix-user の間接CALL変換が HL を潰すため使わない。
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    push hl\n");
            fprintf(out, "    call _%s\n", (node->type == NODE_MUL ? "mul" : "div"));
            fprintf(out, "    pop af\n    pop af\n");
            break;

        // ---- ビット演算 ----
        case NODE_BITAND:
        case NODE_BITOR:
        case NODE_BITXOR: {
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    pop de\n");   // de = 左, hl = 右
            const char *op = node->type == NODE_BITAND ? "and" :
                             node->type == NODE_BITOR  ? "or"  : "xor";
            fprintf(out, "    ld a, l\n    %s e\n    ld l, a\n", op);
            fprintf(out, "    ld a, h\n    %s d\n    ld h, a\n", op);
            break;
        }

        case NODE_BITNOT:
            generate_asm(node->left, out);
            fprintf(out, "    ld a, l\n    cpl\n    ld l, a\n");
            fprintf(out, "    ld a, h\n    cpl\n    ld h, a\n");
            break;

        case NODE_SHL: {
            int l = label_id++;
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    ld a, l\n");     // a = シフト量
            fprintf(out, "    pop hl\n");
            fprintf(out, "    or a\n    jr z, Lshd%d\n", l);
            fprintf(out, "Lshl%d:\n    add hl, hl\n    dec a\n    jr nz, Lshl%d\n", l, l);
            fprintf(out, "Lshd%d:\n", l);
            break;
        }

        case NODE_SHR: {
            int l = label_id++;
            generate_asm(node->left, out);
            fprintf(out, "    push hl\n");
            generate_asm(node->right, out);
            fprintf(out, "    ld a, l\n");
            fprintf(out, "    pop hl\n");
            fprintf(out, "    or a\n    jr z, Lsrd%d\n", l);
            fprintf(out, "Lsrl%d:\n    srl h\n    rr l\n    dec a\n    jr nz, Lsrl%d\n", l, l);
            fprintf(out, "Lsrd%d:\n", l);
            break;
        }

        default:
            generate_asm(node->left, out);
            break;
    }
    generate_asm(node->next, out);
}
