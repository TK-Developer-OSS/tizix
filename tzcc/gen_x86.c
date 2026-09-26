/*
 * gen_x86.c - tzcc の x86-64 バックエンド
 *
 * 出力: GNU as (AT&T 記法) の .s。`gcc out.s -o out` でlibcとリンクして
 *       Linux 上でネイティブ実行できる。crt0 不要（libc をそのまま使う）。
 *
 * 方針（サブセット・Z80版に対応）:
 *   - 値はすべて %rax に置く（Z80 の hl 相当）
 *   - char = 1 バイト / int = ポインタ = 8 バイト
 *   - 変数はグローバル var_NAME（Z80版と同じモデル）。RIP 相対アクセス。
 *   - 関数呼び出しは System V（引数 rdi,rsi,rdx,rcx,r8,r9 / 戻り rax）。
 *     libc の printf/puts/putchar 等をそのまま呼べる。
 *   - main は main:（アンダースコア無し）。他の関数もそのまま。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "generator.h"

static Node *x_root = NULL;
static int   x_lbl = 0;
static int   x_brk[64], x_brk_sp = 0;
static int   x_cont[64], x_cont_sp = 0;

/* ---- シンボル種別（Z80版 find_sym_type と同じ規約） ---- */
/* 0=未検出 / 1=char スカラー / 2=配列 / 3=ポインタ / 4=int スカラー */
static int x_symtype(Node *n, const char *name) {
    if (!n || !name) return 0;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL) && n->value
        && strcmp(n->value, name) == 0) {
        if (n->type == NODE_PTR_DECL) return 3;
        if (n->type == NODE_ARRAY_DECL) return 2;
        return n->base_type == 1 ? 4 : 1;
    }
    int t;
    t = x_symtype(n->left, name);   if (t) return t;
    t = x_symtype(n->right, name);  if (t) return t;
    t = x_symtype(n->third, name);  if (t) return t;
    t = x_symtype(n->fourth, name); if (t) return t;
    return x_symtype(n->next, name);
}
static int x_is_std(const char *n) {
    return n && (!strcmp(n,"stdin") || !strcmp(n,"stdout") || !strcmp(n,"stderr"));
}

static int x_is_int_elem(Node *n, const char *name) {
    if (!n || !name) return 0;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL) && n->value
        && strcmp(n->value, name) == 0)
        return n->base_type == 1;
    int t;
    t = x_is_int_elem(n->left, name);   if (t) return t;
    t = x_is_int_elem(n->right, name);  if (t) return t;
    t = x_is_int_elem(n->third, name);  if (t) return t;
    t = x_is_int_elem(n->fourth, name); if (t) return t;
    return x_is_int_elem(n->next, name);
}

/* ---- 関数フレーム: ローカル変数/仮引数を rbp 相対スロットへ ---- */
/* スコープレスな var_NAME グローバルモデルだと関数間で同名変数が衝突し、
   再帰も壊れる。関数ごとにローカル/仮引数を -off(%rbp) に割り付ける（案B）。*/
static char *x_lname[512];   /* strdup 済み。tzcc は 2次元配列を出せないためポインタ配列 */
static Node *x_lnode[512];
static int   x_loff[512];
static int   x_nl = 0;
static int   x_framesz = 0;

static int x_loc(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < x_nl; i++)
        if (strcmp(x_lname[i], name) == 0) return i;
    return -1;
}
static int x_slotsize(Node *d) {
    if (d->type == NODE_ARRAY_DECL) {
        int n = d->array_size > 0 ? d->array_size : 0;
        if (d->base_type == 1) n *= 8;   /* ポインタ配列: 要素数*8 */
        if (d->left && d->left->type == NODE_STRING && d->left->value) {
            int need = (int)strlen(d->left->value) + 1;
            if (need > n) n = need;
        }
        if (n <= 0) n = 8;
        return (n + 7) & ~7;
    }
    return 8;   /* スカラー / ポインタ */
}
static void x_collect(Node *n) {
    if (!n) return;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL)
        && n->value && x_loc(n->value) < 0 && x_nl < 512) {
        x_framesz += x_slotsize(n);
        x_lname[x_nl] = strdup(n->value);
        x_lnode[x_nl] = n;
        x_loff[x_nl]  = x_framesz;
        x_nl++;
    }
    x_collect(n->left);  x_collect(n->right);
    x_collect(n->third); x_collect(n->fourth);
    x_collect(n->next);
}
/* 変数 name のメモリオペランド（movq/movb 用）を out に書く */
static void x_mem(const char *name, char *out) {
    int i = x_loc(name);
    if (i >= 0) sprintf(out, "-%d(%%rbp)", x_loff[i]);
    else        sprintf(out, "var_%s(%%rip)", name);
}
/* 変数 name のアドレスを rax へ */
static void x_addr_rax(const char *name, FILE *o) {
    int i = x_loc(name);
    if (i >= 0) fprintf(o, "    leaq -%d(%%rbp), %%rax\n", x_loff[i]);
    else        fprintf(o, "    leaq var_%s(%%rip), %%rax\n", name);
}
/* symtype: まず現関数のローカル、無ければグローバル AST を探索 */
static int x_st(const char *name) {
    int i = x_loc(name);
    if (i < 0) return x_symtype(x_root, name);
    Node *d = x_lnode[i];
    if (d->type == NODE_PTR_DECL)   return 3;
    if (d->type == NODE_ARRAY_DECL) return 2;
    return d->base_type == 1 ? 4 : 1;
}
static int x_int_elem(const char *name) {
    int i = x_loc(name);
    if (i < 0) return x_is_int_elem(x_root, name);
    return x_lnode[i]->base_type == 1;
}

/* 文字列を .rodata に置きラベルを %rax へ */
static void x_string(Node *node, FILE *o) {
    unsigned long id = (unsigned long)(uintptr_t)node;
    const char *s = node->value ? node->value : "";
    fprintf(o, "    .section .rodata\n.Lstr_%lu:\n    .byte ", id);
    for (const unsigned char *p = (const unsigned char *)s; ; p++) {
        fprintf(o, "%d,", *p);
        if (!*p) break;
    }
    fprintf(o, "\n    .text\n");
    fprintf(o, "    leaq .Lstr_%lu(%%rip), %%rax\n", id);
}

/* var_name のスカラーを %rax へ（char は 0拡張） */
static void x_load_scalar(const char *name, int st, FILE *o) {
    char m[80]; x_mem(name, m);
    if (st == 1) fprintf(o, "    movzbq %s, %%rax\n", m);
    else         fprintf(o, "    movq %s, %%rax\n", m);
}
static void x_store_scalar(const char *name, int st, FILE *o) {
    char m[80]; x_mem(name, m);
    if (st == 1) fprintf(o, "    movb %%al, %s\n", m);
    else         fprintf(o, "    movq %%rax, %s\n", m);
}

void gen_x86(Node *node, FILE *o);

/* 比較: 左 op 右 -> %rax(0/1) */
static void x_cmp(Node *node, FILE *o) {
    gen_x86(node->left, o);
    fprintf(o, "    pushq %%rax\n");
    gen_x86(node->right, o);
    fprintf(o, "    movq %%rax, %%rcx\n    popq %%rax\n"); /* rax=左, rcx=右 */
    fprintf(o, "    cmpq %%rcx, %%rax\n");
    const char *cc = "e";
    switch (node->type) {
        case NODE_EQ: cc = "e";  break;
        case NODE_NE: cc = "ne"; break;
        case NODE_LT: cc = "l";  break;
        case NODE_GT: cc = "g";  break;
        case NODE_LE: cc = "le"; break;
        case NODE_GE: cc = "ge"; break;
        default: break;
    }
    fprintf(o, "    set%s %%al\n    movzbq %%al, %%rax\n", cc);
}

/* &a[i] / a[i] の要素アドレスを %rax へ。size=要素サイズ */
static int x_elem_addr(Node *idxnode, FILE *o) {
    Node *b = idxnode->left;
    int esz = idxnode->array_size;
    if (b->type == NODE_IDENTIFIER) {
        int st = x_st(b->value);
        if (esz <= 0) esz = x_int_elem(b->value) ? 8 : 1;
        if (st == 3) { char m[80]; x_mem(b->value, m);
                       fprintf(o, "    movq %s, %%rax\n", m); }        /* ポインタ値 */
        else         x_addr_rax(b->value, o);                         /* 配列先頭 */
    } else {
        if (esz <= 0) {
            esz = 8;
            /* b が `ident[k]`（ident は char** / char*[]）なら結果は char*。
               外側の添字は 1 バイト刻み（argv[i][j] の j）。 */
            if (b->type == NODE_INDEX && b->left && b->left->type == NODE_IDENTIFIER
                && x_int_elem(b->left->value))
                esz = 1;
        }
        gen_x86(b, o);                  /* base アドレス/ポインタ値 -> rax */
    }
    fprintf(o, "    pushq %%rax\n");
    gen_x86(idxnode->right, o);         /* 添字 -> rax */
    if (esz == 8)      fprintf(o, "    shlq $3, %%rax\n");
    else if (esz == 4) fprintf(o, "    shlq $2, %%rax\n");
    else if (esz == 2) fprintf(o, "    shlq $1, %%rax\n");
    else if (esz != 1) fprintf(o, "    imulq $%d, %%rax\n", esz);
    fprintf(o, "    popq %%rcx\n    addq %%rcx, %%rax\n");
    return esz;
}

/* ++/-- の一般形（a[i]++ / p->m++ / v.m++）用: target のアドレスを %rax へ置き、
   要素バイトサイズ（1 or 8）を返す。*/
static int x_lval_addr(Node *t, FILE *o) {
    if (t->type == NODE_INDEX) {
        return x_elem_addr(t, o);
    } else if (t->type == NODE_MEMBER) {
        int save = t->base_type;
        t->base_type = 2;              /* アドレスモード */
        gen_x86(t, o);
        t->base_type = save;
        return 8;
    } else if (t->type == NODE_IDENTIFIER) {
        int st = x_st(t->value);
        x_addr_rax(t->value, o);
        return (st == 1) ? 1 : 8;
    }
    fprintf(o, "    movq $0, %%rax\n");
    return 8;
}

/* System V 整数引数レジスタ。tzcc 自身が配列初期化子 {..} を出せないため関数で。*/
static const char *argreg(int i) {
    if (i == 0) return "%rdi";
    if (i == 1) return "%rsi";
    if (i == 2) return "%rdx";
    if (i == 3) return "%rcx";
    if (i == 4) return "%r8";
    return "%r9";
}
static const char *argreg8(int i) {   /* 対応する 8bit 名 */
    if (i == 0) return "%dil";
    if (i == 1) return "%sil";
    if (i == 2) return "%dl";
    if (i == 3) return "%cl";
    if (i == 4) return "%r8b";
    return "%r9b";
}

/* x86 は int/long/ポインタが同じ 8byte。宣言ノードの base_type==2(long) を
   1(int) へ均して既存の 16bit 前提コード(x_symtype 等)をそのまま使う。
   base_type==2 は INDEX/MEMBER のアドレスモード用にも使われるが、それは
   codegen 中に一時的に立てるもので、パース直後のツリーには宣言ノードにしか
   base_type==2 は無い。*/
static void x_demote_long(Node *n) {
    if (!n) return;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL)
        && n->base_type == 2)
        n->base_type = 1;
    x_demote_long(n->left);  x_demote_long(n->right);
    x_demote_long(n->third); x_demote_long(n->fourth);
    x_demote_long(n->next);
}

void gen_x86(Node *node, FILE *o) {
    if (!node) return;
    switch (node->type) {
        case NODE_ROOT:
            x_root = node;
            fprintf(o, "    .text\n");
            gen_x86(node->left, o);
            break;

        case NODE_FUNC: {
            const char *nm = node->value;
            Node *params = node->right ? node->left : NULL;
            Node *body   = node->right ? node->right : node->left;
            x_nl = 0; x_framesz = 0;
            x_collect(params);
            x_collect(body);
            int frame = (x_framesz + 15) & ~15;
            fprintf(o, "    .globl %s\n%s:\n", nm, nm);
            fprintf(o, "    pushq %%rbp\n    movq %%rsp, %%rbp\n");
            if (frame) fprintf(o, "    subq $%d, %%rsp\n", frame);
            int i = 0;
            for (Node *p = params; p && i < 6; p = p->next, i++) {
                char m[80]; x_mem(p->value, m);
                int st = (p->type == NODE_PTR_DECL) ? 3 : (p->base_type == 1 ? 4 : 1);
                if (st == 1) {
                    fprintf(o, "    movb %s, %s\n", argreg8(i), m);
                } else {
                    fprintf(o, "    movq %s, %s\n", argreg(i), m);
                }
            }
            if (body) gen_x86(body, o);
            fprintf(o, "    movq $0, %%rax\n    movq %%rbp, %%rsp\n    popq %%rbp\n    ret\n");
            x_nl = 0;   /* 関数外へ: 以降の参照はグローバル扱いに戻す */
            break;
        }

        case NODE_BLOCK:
            gen_x86(node->left, o);
            break;

        case NODE_ARRAY_DECL: {
            /* ローカル配列の文字列初期化: リテラルをスタックスロットへ書き込む */
            int li = x_loc(node->value);
            if (li >= 0 && node->left && node->left->type == NODE_STRING && node->left->value) {
                int off = x_loff[li];
                int k = 0;
                for (const unsigned char *p = (const unsigned char *)node->left->value; ; p++, k++) {
                    fprintf(o, "    movb $%d, -%d(%%rbp)\n", *p, off - k);
                    if (!*p) break;
                }
            }
            break;
        }

        case NODE_VAR_DECL: {
            if (node->left) {
                int dst = (node->base_type == 1) ? 4 : 1;
                if (node->left->type == NODE_STRING) {
                    fprintf(o, "    movq $%d, %%rax\n", (unsigned char)node->left->value[0]);
                    x_store_scalar(node->value, dst, o);
                } else {
                    gen_x86(node->left, o);
                    x_store_scalar(node->value, dst, o);
                }
            }
            break;
        }

        case NODE_PTR_DECL:
            if (node->left) {
                if (node->left->type == NODE_STRING || node->left->type == NODE_ARG)
                    x_string(node->left, o);
                else
                    gen_x86(node->left, o);
                char m[80]; x_mem(node->value, m);
                fprintf(o, "    movq %%rax, %s\n", m);
            }
            break;

        case NODE_ASSIGN: {
            int st = x_st(node->value);
            if (!st) st = 4;
            if (node->left->type == NODE_STRING || node->left->type == NODE_ARG)
                x_string(node->left, o);
            else
                gen_x86(node->left, o);
            x_store_scalar(node->value, (st == 3 ? 3 : st), o);
            break;
        }

        case NODE_STORE_INDEX: {
            gen_x86(node->third, o);           /* 値 -> rax */
            fprintf(o, "    pushq %%rax\n");
            int esz = x_elem_addr(node, o);    /* 要素アドレス -> rax */
            fprintf(o, "    popq %%rcx\n");     /* rcx = 値 */
            if (esz >= 8) fprintf(o, "    movq %%rcx, (%%rax)\n");
            else          fprintf(o, "    movb %%cl, (%%rax)\n");
            break;
        }
        case NODE_INDEX: {
            int esz = x_elem_addr(node, o);
            if (node->base_type == 2) break;  /* 多次元途中: アドレスのまま */
            if (esz >= 8) fprintf(o, "    movq (%%rax), %%rax\n");
            else          fprintf(o, "    movzbq (%%rax), %%rax\n");
            break;
        }

        case NODE_DEREF:
            gen_x86(node->left, o);
            fprintf(o, "    movzbq (%%rax), %%rax\n");
            break;
        case NODE_STORE_DEREF:
            gen_x86(node->right, o);
            fprintf(o, "    pushq %%rax\n");
            gen_x86(node->left, o);
            fprintf(o, "    popq %%rcx\n    movb %%cl, (%%rax)\n");
            break;
        case NODE_ADDR:
            if (node->left && node->left->type == NODE_IDENTIFIER)
                x_addr_rax(node->left->value, o);
            else if (node->left && node->left->type == NODE_INDEX)
                x_elem_addr(node->left, o);
            else if (node->left && node->left->type == NODE_MEMBER) {
                int save = node->left->base_type;
                node->left->base_type = 2;
                gen_x86(node->left, o);
                node->left->base_type = save;
            } else
                fprintf(o, "    movq $0, %%rax\n");
            break;

        case NODE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) { char m[80]; x_mem(b->value, m);
                             fprintf(o, "    movq %s, %%rax\n", m); }
                else       x_addr_rax(b->value, o);
            } else {
                gen_x86(b, o);
            }
            if (node->array_size) fprintf(o, "    addq $%d, %%rax\n", node->array_size);
            if (node->base_type == 2) break;   /* 配列メンバ: アドレスを返す */
            if (node->esz == 1) fprintf(o, "    movzbq (%%rax), %%rax\n");   /* char メンバ */
            else                fprintf(o, "    movq (%%rax), %%rax\n");
            break;
        }
        case NODE_STORE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            gen_x86(node->right, o);
            fprintf(o, "    pushq %%rax\n");
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) { char m[80]; x_mem(b->value, m);
                             fprintf(o, "    movq %s, %%rax\n", m); }
                else       x_addr_rax(b->value, o);
            } else {
                gen_x86(b, o);
            }
            if (node->array_size) fprintf(o, "    addq $%d, %%rax\n", node->array_size);
            if (node->esz == 1) fprintf(o, "    popq %%rcx\n    movb %%cl, (%%rax)\n");   /* char メンバ */
            else                fprintf(o, "    popq %%rcx\n    movq %%rcx, (%%rax)\n");
            break;
        }

        case NODE_CALL: {
            int argc = 0;
            Node *args[6];
            for (Node *a = node->left; a && argc < 6; a = a->next) args[argc++] = a;
            /* 右から評価してスタックへ、後で引数レジスタへ */
            for (int i = argc - 1; i >= 0; i--) {
                Node *a = args[i];
                if (a->type == NODE_STRING || a->type == NODE_ARG) x_string(a, o);
                else if (a->type == NODE_IDENTIFIER) {
                    if (x_is_std(a->value)) fprintf(o, "    movq %s(%%rip), %%rax\n", a->value);
                    else {
                        int st = x_st(a->value);
                        if (st == 2) x_addr_rax(a->value, o);
                        else if (st == 1) x_load_scalar(a->value, 1, o);
                        else { char m[80]; x_mem(a->value, m);
                               fprintf(o, "    movq %s, %%rax\n", m); }
                    }
                } else if (a->type == NODE_NUMBER) {
                    fprintf(o, "    movq $%s, %%rax\n", a->value);
                } else {
                    Node *sv = a->next; a->next = NULL;
                    gen_x86(a, o);
                    a->next = sv;
                }
                fprintf(o, "    pushq %%rax\n");
            }
            for (int i = 0; i < argc; i++)
                fprintf(o, "    popq %s\n", argreg(i));
            fprintf(o, "    xorl %%eax, %%eax\n");        /* 可変長引数の SSE 個数 = 0 */
            /* tzcc の 1引数 putc(c) は libc では putchar に相当 */
            const char *cn = node->value;
            if (strcmp(cn, "putc") == 0) cn = "putchar";
            fprintf(o, "    call %s\n", cn);
            break;
        }

        case NODE_RETURN:
            if (node->left) gen_x86(node->left, o);
            fprintf(o, "    movq %%rbp, %%rsp\n    popq %%rbp\n    ret\n");
            break;

        case NODE_NUMBER:
            fprintf(o, "    movq $%s, %%rax\n", node->value);
            break;

        case NODE_STRING:
            x_string(node, o);
            break;

        case NODE_IDENTIFIER: {
            if (x_is_std(node->value)) { fprintf(o, "    movq %s(%%rip), %%rax\n", node->value); break; }
            int st = x_st(node->value);
            if (st == 2)      x_addr_rax(node->value, o);
            else if (st == 1) x_load_scalar(node->value, 1, o);
            else { char m[80]; x_mem(node->value, m);
                   fprintf(o, "    movq %s, %%rax\n", m); }
            break;
        }

        case NODE_ADD: case NODE_SUB: case NODE_MUL: case NODE_DIV: {
            gen_x86(node->left, o);
            fprintf(o, "    pushq %%rax\n");
            gen_x86(node->right, o);
            fprintf(o, "    movq %%rax, %%rcx\n    popq %%rax\n"); /* rax=左, rcx=右 */
            if (node->type == NODE_ADD) fprintf(o, "    addq %%rcx, %%rax\n");
            else if (node->type == NODE_SUB) fprintf(o, "    subq %%rcx, %%rax\n");
            else if (node->type == NODE_MUL) fprintf(o, "    imulq %%rcx, %%rax\n");
            else { fprintf(o, "    cqto\n    idivq %%rcx\n"); }
            break;
        }

        case NODE_NEG:
            gen_x86(node->left, o);
            fprintf(o, "    negq %%rax\n");
            break;
        case NODE_BITNOT:
            gen_x86(node->left, o);
            fprintf(o, "    notq %%rax\n");
            break;
        case NODE_NOT: {
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n    sete %%al\n    movzbq %%al, %%rax\n");
            break;
        }

        case NODE_BITAND: case NODE_BITOR: case NODE_BITXOR: {
            gen_x86(node->left, o);
            fprintf(o, "    pushq %%rax\n");
            gen_x86(node->right, o);
            fprintf(o, "    movq %%rax, %%rcx\n    popq %%rax\n");
            fprintf(o, node->type == NODE_BITAND ? "    andq %%rcx, %%rax\n" :
                       node->type == NODE_BITOR  ? "    orq %%rcx, %%rax\n"  :
                                                   "    xorq %%rcx, %%rax\n");
            break;
        }
        case NODE_SHL: case NODE_SHR: {
            gen_x86(node->left, o);
            fprintf(o, "    pushq %%rax\n");
            gen_x86(node->right, o);
            fprintf(o, "    movq %%rax, %%rcx\n    popq %%rax\n");
            fprintf(o, node->type == NODE_SHL ? "    shlq %%cl, %%rax\n" : "    shrq %%cl, %%rax\n");
            break;
        }

        case NODE_EQ: case NODE_NE: case NODE_LT: case NODE_GT: case NODE_LE: case NODE_GE:
            x_cmp(node, o);
            break;

        case NODE_AND: {
            int l = x_lbl++;
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n    jz .Lf%d\n", l);
            gen_x86(node->right, o);
            fprintf(o, "    testq %%rax, %%rax\n    jz .Lf%d\n", l);
            fprintf(o, "    movq $1, %%rax\n    jmp .Le%d\n.Lf%d:\n    movq $0, %%rax\n.Le%d:\n", l, l, l);
            break;
        }
        case NODE_OR: {
            int l = x_lbl++;
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n    jnz .Lt%d\n", l);
            gen_x86(node->right, o);
            fprintf(o, "    testq %%rax, %%rax\n    jnz .Lt%d\n", l);
            fprintf(o, "    movq $0, %%rax\n    jmp .Le%d\n.Lt%d:\n    movq $1, %%rax\n.Le%d:\n", l, l, l);
            break;
        }
        case NODE_TERNARY: {
            int l = x_lbl++;
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n    jz .Ltf%d\n", l);
            gen_x86(node->right, o);
            fprintf(o, "    jmp .Lte%d\n.Ltf%d:\n", l, l);
            gen_x86(node->third, o);
            fprintf(o, ".Lte%d:\n", l);
            break;
        }

        case NODE_PREINC: case NODE_PREDEC: {
            if (node->value) {
                int st = x_st(node->value); if (!st) st = 4;
                x_load_scalar(node->value, st, o);
                fprintf(o, node->type == NODE_PREINC ? "    incq %%rax\n" : "    decq %%rax\n");
                x_store_scalar(node->value, st, o);
            } else {
                int esz = x_lval_addr(node->left, o);           /* rax = アドレス */
                fprintf(o, "    pushq %%rax\n");
                fprintf(o, esz >= 8 ? "    movq (%%rax), %%rax\n" : "    movzbq (%%rax), %%rax\n");
                fprintf(o, node->type == NODE_PREINC ? "    incq %%rax\n" : "    decq %%rax\n");
                fprintf(o, "    movq %%rax, %%rcx\n    popq %%rdx\n");
                fprintf(o, esz >= 8 ? "    movq %%rcx, (%%rdx)\n" : "    movb %%cl, (%%rdx)\n");
                fprintf(o, "    movq %%rcx, %%rax\n");           /* 評価結果 = 新しい値 */
            }
            break;
        }
        case NODE_POSTINC: case NODE_POSTDEC: {
            if (node->value) {
                int st = x_st(node->value); if (!st) st = 4;
                x_load_scalar(node->value, st, o);
                fprintf(o, "    pushq %%rax\n");
                fprintf(o, node->type == NODE_POSTINC ? "    incq %%rax\n" : "    decq %%rax\n");
                x_store_scalar(node->value, st, o);
                fprintf(o, "    popq %%rax\n");
            } else {
                int esz = x_lval_addr(node->left, o);           /* rax = アドレス */
                fprintf(o, "    pushq %%rax\n");
                fprintf(o, esz >= 8 ? "    movq (%%rax), %%rax\n" : "    movzbq (%%rax), %%rax\n");
                fprintf(o, "    pushq %%rax\n");
                fprintf(o, node->type == NODE_POSTINC ? "    incq %%rax\n" : "    decq %%rax\n");
                fprintf(o, "    movq %%rax, %%rcx\n");
                fprintf(o, "    popq %%rax\n");                  /* rax = 旧値（評価結果） */
                fprintf(o, "    popq %%rdx\n");                  /* rdx = アドレス */
                fprintf(o, esz >= 8 ? "    movq %%rcx, (%%rdx)\n" : "    movb %%cl, (%%rdx)\n");
            }
            break;
        }

        case NODE_IF: {
            int l = x_lbl++;
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n");
            if (node->third) {
                fprintf(o, "    jz .Lelse%d\n", l);
                if (node->right) gen_x86(node->right, o);
                fprintf(o, "    jmp .Lend%d\n.Lelse%d:\n", l, l);
                gen_x86(node->third, o);
                fprintf(o, ".Lend%d:\n", l);
            } else {
                fprintf(o, "    jz .Lend%d\n", l);
                if (node->right) gen_x86(node->right, o);
                fprintf(o, ".Lend%d:\n", l);
            }
            break;
        }
        case NODE_WHILE: {
            int l = x_lbl++;
            x_cont[x_cont_sp++] = l; x_brk[x_brk_sp++] = l;
            fprintf(o, ".Lcont%d:\n", l);
            gen_x86(node->left, o);
            fprintf(o, "    testq %%rax, %%rax\n    jz .Lbrk%d\n", l);
            if (node->right) gen_x86(node->right, o);
            fprintf(o, "    jmp .Lcont%d\n.Lbrk%d:\n", l, l);
            x_cont_sp--; x_brk_sp--;
            break;
        }
        case NODE_FOR: {
            int l = x_lbl++;
            x_cont[x_cont_sp++] = l; x_brk[x_brk_sp++] = l;
            if (node->left) gen_x86(node->left, o);
            fprintf(o, ".Lbeg%d:\n", l);
            if (node->right) {
                gen_x86(node->right, o);
                fprintf(o, "    testq %%rax, %%rax\n    jz .Lbrk%d\n", l);
            }
            if (node->fourth) gen_x86(node->fourth, o);
            fprintf(o, ".Lcont%d:\n", l);
            if (node->third) gen_x86(node->third, o);
            fprintf(o, "    jmp .Lbeg%d\n.Lbrk%d:\n", l, l);
            x_cont_sp--; x_brk_sp--;
            break;
        }
        case NODE_BREAK:
            if (x_brk_sp) fprintf(o, "    jmp .Lbrk%d\n", x_brk[x_brk_sp - 1]);
            break;
        case NODE_CONTINUE:
            if (x_cont_sp) fprintf(o, "    jmp .Lcont%d\n", x_cont[x_cont_sp - 1]);
            break;

        case NODE_CASE:
            fprintf(o, ".Lcase%d_%d:\n", node->array_size, node->base_type);
            break;
        case NODE_DEFAULT:
            fprintf(o, ".Ldef%d:\n", node->array_size);
            break;
        case NODE_SWITCH: {
            int l = x_lbl++;
            x_brk[x_brk_sp++] = l;
            gen_x86(node->left, o);
            Node *b = node->right ? node->right->left : NULL;
            int has_def = 0;
            int ci = 0;
            for (Node *p = b; p; p = p->next) {
                if (p->type == NODE_CASE && p->value) {
                    p->array_size = l; p->base_type = ci;
                    fprintf(o, "    cmpq $%s, %%rax\n    je .Lcase%d_%d\n", p->value, l, ci);
                    ci++;
                } else if (p->type == NODE_DEFAULT) { p->array_size = l; has_def = 1; }
            }
            fprintf(o, has_def ? "    jmp .Ldef%d\n" : "    jmp .Lbrk%d\n", l);
            for (Node *p = b; p; p = p->next) {
                Node *sv = p->next; p->next = NULL;
                gen_x86(p, o);
                p->next = sv;
            }
            fprintf(o, ".Lbrk%d:\n", l);
            x_brk_sp--;
            break;
        }

        case NODE_CVT:
            // x86 は int/long/ptr が同幅(8byte)。キャストは値を素通し。
            gen_x86(node->left, o);
            break;

        default:
            gen_x86(node->left, o);
            break;
    }
    gen_x86(node->next, o);
}

/* ---- データ領域 ---- */
static char *xd_seen[1024]; static int xd_seen_n = 0;
static int xd_dup(const char *name) {
    for (int i = 0; i < xd_seen_n; i++) if (strcmp(xd_seen[i], name) == 0) return 1;
    if (xd_seen_n < 1024) xd_seen[xd_seen_n++] = strdup(name);
    return 0;
}

static void x_emit_data(Node *node, FILE *o) {
    if (!node) return;
    if ((node->type == NODE_VAR_DECL || node->type == NODE_PTR_DECL || node->type == NODE_ARRAY_DECL)
        && node->value && xd_dup(node->value)) {
        x_emit_data(node->left, o);  x_emit_data(node->right, o);
        x_emit_data(node->third, o); x_emit_data(node->fourth, o);
        x_emit_data(node->next, o);
        return;
    }
    if (node->type == NODE_VAR_DECL) {
        fprintf(o, "    .data\nvar_%s:\n", node->value);
        fprintf(o, node->base_type == 1 ? "    .quad 0\n" : "    .byte 0\n");
    } else if (node->type == NODE_PTR_DECL) {
        fprintf(o, "    .data\nvar_%s:\n    .quad 0\n", node->value);
    } else if (node->type == NODE_ARRAY_DECL) {
        int n = node->array_size > 0 ? node->array_size : 1;
        if (node->left && node->left->type == NODE_STRING && node->left->value) {
            const char *s = node->left->value;
            int slen = (int)strlen(s);
            fprintf(o, "    .data\nvar_%s:\n    .byte ", node->value);
            for (const unsigned char *p = (const unsigned char *)s; ; p++) {
                fprintf(o, "%d,", *p); if (!*p) break;
            }
            fprintf(o, "\n");
            if (n > slen + 1) fprintf(o, "    .zero %d\n", n - (slen + 1));
        } else if (node->left && node->left->type == NODE_INITLIST) {
            int esz = (node->base_type == 1) ? 8 : 1;   // x86: int/long は 8byte
            int cnt = 0;
            fprintf(o, "    .data\nvar_%s:\n", node->value);
            for (Node *e = node->left->left; e; e = e->next) {
                long v = (e->type == NODE_NUMBER) ? strtol(e->value, NULL, 0) : 0;
                fprintf(o, esz == 1 ? "    .byte %ld\n" : "    .quad %ld\n", v);
                cnt++;
            }
            if (n > cnt) fprintf(o, "    .zero %d\n", (n - cnt) * esz);
        } else {
            if (node->base_type == 1) n *= 8;
            fprintf(o, "    .bss\nvar_%s:\n    .zero %d\n", node->value, n);
        }
    }
    if (node->type != NODE_ARRAY_DECL) x_emit_data(node->left, o);
    x_emit_data(node->right, o);
    x_emit_data(node->third, o);
    x_emit_data(node->fourth, o);
    x_emit_data(node->next, o);
}

void generate_x86(Node *root, FILE *out) {
    fprintf(out, "# Generated by tzcc (x86-64 backend)\n");
    x_demote_long(root);   // long(base_type==2) を int(1) 扱いに（x86 は同幅 8byte）
    x_emit_data(root->left, out);
    fprintf(out, "    .text\n");
    x_root = root;
    gen_x86(root->left, out);
}
