/*
 * gen_ia16.c - tzcc の x86 16bit(8086 リアルモード)バックエンド [PoC]
 *
 * 出力: GNU as (AT&T 記法, .code16) の .s。ia16-elf-as で 8086 命令だけに落ちる。
 *
 * この PoC の目的: **SS != DS でも正しいコードを出せるか**(arch/x86-ia16/X86IA16.md の案 E)。
 *   - ローカル変数/仮引数は %bp 相対(= 既定セグメントが SS)。
 *   - グローバル/文字列は絶対アドレス(= DS)。
 *   - ローカル配列/ローカルのアドレスを介したメモリアクセスは %ss: を付けて出す。
 *   - ポインタは 16bit のオフセットだけ。DS 側のつもりのポインタに SS 側のアドレスが
 *     入る(= 関数へ渡す/変数へ代入する/返す)と、受け側が DS で読んで壊れる。
 *     これを「SS ポインタの脱出」として検出し、stderr に列挙する(出力は止めない)。
 *     カーネルの C をこの規則で数えれば、案 E に要る型の規模が分かる。
 *
 * 方針(gen_x86.c と同じ骨格):
 *   - 値はすべて %ax(Z80 の hl 相当)。int = ポインタ = 2 バイト、char = 1 バイト。
 *   - 2 項演算: 左→push、右→%cx、pop %ax。条件分岐は 8086 の jcc(±127)に収まらないので
 *     「反転 jcc で jmp をまたぐ」形で出す(jcc rel16 は 386 以降)。
 *   - 呼び出し規約は cdecl 風: 引数は右から push、呼び出し側が %sp を戻す、戻り値は %ax。
 *   - アドレスレジスタは %bx。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "generator.h"

static Node *a_root = NULL;
static int   a_lbl = 0;
static int   a_brk[64], a_brk_sp = 0;
static int   a_cont[64], a_cont_sp = 0;

/* ---- 診断(SS ポインタの脱出) ---- */
static const char *a_fn = "";
static int a_escapes = 0;

/* ---- シンボル種別(Z80版 find_sym_type と同じ規約) ---- */
/* 0=未検出 / 1=char スカラー / 2=配列 / 3=ポインタ / 4=int スカラー */
static int a_symtype(Node *n, const char *name) {
    if (!n || !name) return 0;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL) && n->value
        && strcmp(n->value, name) == 0) {
        if (n->type == NODE_PTR_DECL) return 3;
        if (n->type == NODE_ARRAY_DECL) return 2;
        return n->base_type >= 1 ? 4 : 1;
    }
    int t;
    t = a_symtype(n->left, name);   if (t) return t;
    t = a_symtype(n->right, name);  if (t) return t;
    t = a_symtype(n->third, name);  if (t) return t;
    t = a_symtype(n->fourth, name); if (t) return t;
    return a_symtype(n->next, name);
}
static int a_is_std(const char *n) {
    return n && (!strcmp(n,"stdin") || !strcmp(n,"stdout") || !strcmp(n,"stderr"));
}
static int a_is_int_elem(Node *n, const char *name) {
    if (!n || !name) return 0;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL) && n->value
        && strcmp(n->value, name) == 0)
        return n->base_type >= 1;
    int t;
    t = a_is_int_elem(n->left, name);   if (t) return t;
    t = a_is_int_elem(n->right, name);  if (t) return t;
    t = a_is_int_elem(n->third, name);  if (t) return t;
    t = a_is_int_elem(n->fourth, name); if (t) return t;
    return a_is_int_elem(n->next, name);
}

/* ---- 関数フレーム: ローカル/仮引数を bp 相対スロットへ ---- */
static char *a_lname[512];
static Node *a_lnode[512];
static int   a_loff[512];
static int   a_nl = 0;
static int   a_framesz = 0;

static int a_loc(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < a_nl; i++)
        if (strcmp(a_lname[i], name) == 0) return i;
    return -1;
}
static int a_slotsize(Node *d) {
    if (d->type == NODE_ARRAY_DECL) {
        int n = d->array_size > 0 ? d->array_size : 0;
        if (d->base_type >= 1) n *= 2;      /* int/ポインタ配列: 要素数*2 */
        if (d->left && d->left->type == NODE_STRING && d->left->value) {
            int need = (int)strlen(d->left->value) + 1;
            if (need > n) n = need;
        }
        if (n <= 0) n = 2;
        return (n + 1) & ~1;
    }
    return 2;   /* スカラー / ポインタ(char も 2 バイト枠。bp 相対を偶数に保つ) */
}
static void a_collect(Node *n) {
    if (!n) return;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL)
        && n->value && a_loc(n->value) < 0 && a_nl < 512) {
        a_framesz += a_slotsize(n);
        a_lname[a_nl] = strdup(n->value);
        a_lnode[a_nl] = n;
        a_loff[a_nl]  = a_framesz;
        a_nl++;
    }
    a_collect(n->left);  a_collect(n->right);
    a_collect(n->third); a_collect(n->fourth);
    a_collect(n->next);
}
/* globals the code really refers to (#201): a_emit_data defines var_NAME only for these. It used to define one for
 * every declaration in the tree, function locals and parameters included, which are on the stack and never read
 * through var_ (about 500B of dead .data in the kernel; and the DRIVER, which must have no data at all). */
static char *a_refs[4096]; static int a_nref = 0;
static void a_ref(const char *name) {
    for (int i = 0; i < a_nref; i++) if (strcmp(a_refs[i], name) == 0) return;
    if (a_nref < 4096) a_refs[a_nref++] = strdup(name);
}
static int a_is_ref(const char *name) {
    for (int i = 0; i < a_nref; i++) if (strcmp(a_refs[i], name) == 0) return 1;
    return 0;
}
static void a_mem(const char *name, char *out) {
    int i = a_loc(name);
    if (i >= 0) sprintf(out, "-%d(%%bp)", a_loff[i]);
    else      { sprintf(out, "var_%s", name); a_ref(name); }
}
static void a_addr_ax(const char *name, FILE *o) {
    int i = a_loc(name);
    if (i >= 0) fprintf(o, "    leaw -%d(%%bp), %%ax\n", a_loff[i]);
    else      { fprintf(o, "    movw $var_%s, %%ax\n", name); a_ref(name); }
}
static int a_st(const char *name) {
    int i = a_loc(name);
    if (i < 0) return a_symtype(a_root, name);
    Node *d = a_lnode[i];
    if (d->type == NODE_PTR_DECL)   return 3;
    if (d->type == NODE_ARRAY_DECL) return 2;
    return d->base_type >= 1 ? 4 : 1;
}
static int a_int_elem(const char *name) {
    int i = a_loc(name);
    if (i < 0) return a_is_int_elem(a_root, name);
    return a_lnode[i]->base_type >= 1;
}

/* ---- SS 側のアドレスか ----
   ローカル配列/ローカル構造体の先頭、そのアドレス(&local, &local[i], &local.m)、
   およびそれに足し引きしたもの。ローカルの **ポインタ変数** の値は DS 側のつもりなので含めない。 */
static int a_is_ss_base(Node *b) {     /* INDEX/MEMBER/DEREF の「基底」が SS 側か */
    if (!b) return 0;
    if (b->type == NODE_IDENTIFIER) return a_loc(b->value) >= 0 && a_st(b->value) == 2;
    return 0;
}
static int a_is_ss(Node *n) {
    if (!n) return 0;
    switch (n->type) {
    case NODE_IDENTIFIER:
        return a_loc(n->value) >= 0 && a_st(n->value) == 2;
    case NODE_ADDR: {
        Node *t = n->left;
        if (!t) return 0;
        if (t->type == NODE_IDENTIFIER) return a_loc(t->value) >= 0;
        if (t->type == NODE_INDEX) return a_is_ss_base(t->left) || a_is_ss(t->left);
        if (t->type == NODE_MEMBER) {
            int arrow = t->value && t->value[0] == '-';
            if (arrow) return a_is_ss(t->left);
            return t->left && t->left->type == NODE_IDENTIFIER ? a_loc(t->left->value) >= 0
                                                                : a_is_ss(t->left);
        }
        return 0;
    }
    case NODE_ADD:  return a_is_ss(n->left) || a_is_ss(n->right);
    case NODE_SUB:  return a_is_ss(n->left);
    case NODE_CVT:  return a_is_ss(n->left);
    default:        return 0;
    }
}
static const char *a_seg(int ss) { return ss ? "%ss:" : ""; }

static void a_escape(Node *e, const char *what) {
    if (!e || !a_is_ss(e)) return;
    a_escapes++;
    fprintf(stderr, "ia16: SS pointer escapes into DS (%s) in %s()\n", what, a_fn);
}

/* 文字列を .data に置きラベルを %ax へ */
static void a_string(Node *node, FILE *o) {
    unsigned long id = (unsigned long)(uintptr_t)node;
    const char *s = node->value ? node->value : "";
    fprintf(o, "    .data\n.Lstr_%lu:\n    .byte ", id);
    for (const unsigned char *p = (const unsigned char *)s; ; p++) {
        fprintf(o, "%d%s", *p, *p ? "," : "");
        if (!*p) break;
    }
    fprintf(o, "\n    .previous\n");
    fprintf(o, "    movw $.Lstr_%lu, %%ax\n", id);
}

static void a_load_scalar(const char *name, int st, FILE *o) {
    char m[80]; a_mem(name, m);
    if (st == 1) fprintf(o, "    movb %s, %%al\n    xorb %%ah, %%ah\n", m);
    else         fprintf(o, "    movw %s, %%ax\n", m);
}
static void a_store_scalar(const char *name, int st, FILE *o) {
    char m[80]; a_mem(name, m);
    if (st == 1) fprintf(o, "    movb %%al, %s\n", m);
    else         fprintf(o, "    movw %%ax, %s\n", m);
}

/* 条件ジャンプ: 8086 の jcc は ±127。反転 jcc で jmp をまたぐ。 */
static void a_jcc(const char *inv, const char *label, FILE *o) {
    fprintf(o, "    j%s 1f\n    jmp %s\n1:\n", inv, label);
}

void gen_ia16(Node *node, FILE *o);

/* An operand that loads with one instruction and has no side effects: a constant, or a scalar /
   pointer / array name. Such operands go straight into %cx instead of through push/pop. */
static int a_simple(Node *n) {
    if (!n) return 0;
    if (n->type == NODE_NUMBER) return 1;
    if (n->type == NODE_IDENTIFIER) return !a_is_std(n->value);
    return 0;
}
static void a_load_cx(Node *n, FILE *o) {
    if (n->type == NODE_NUMBER) { fprintf(o, "    movw $%s, %%cx\n", n->value); return; }
    int st = a_st(n->value);
    char m[80];
    if (st == 2) {
        if (a_loc(n->value) >= 0) fprintf(o, "    leaw -%d(%%bp), %%cx\n", a_loff[a_loc(n->value)]);
        else                    { fprintf(o, "    movw $var_%s, %%cx\n", n->value); a_ref(n->value); }
        return;
    }
    a_mem(n->value, m);
    if (st == 1) fprintf(o, "    movb %s, %%cl\n    xorb %%ch, %%ch\n", m);
    else         fprintf(o, "    movw %s, %%cx\n", m);
}
/* %ax = left, %cx = right */
static void a_operands(Node *l, Node *r, FILE *o) {
    if (a_simple(r)) {
        gen_ia16(l, o);
        a_load_cx(r, o);
    } else if (a_simple(l)) {
        gen_ia16(r, o);
        fprintf(o, "    movw %%ax, %%cx\n");
        gen_ia16(l, o);
    } else {
        gen_ia16(l, o);
        fprintf(o, "    pushw %%ax\n");
        gen_ia16(r, o);
        fprintf(o, "    movw %%ax, %%cx\n    popw %%ax\n");
    }
}
/* Operand text usable directly as the source of cmpw ("$5" or a memory operand); 0 if there is none. */
static int a_cmp_src(Node *r, char *out) {
    if (r->type == NODE_NUMBER) { sprintf(out, "$%s", r->value); return 1; }
    if (r->type == NODE_IDENTIFIER && !a_is_std(r->value)) {
        int st = a_st(r->value);
        if (st == 3 || st == 4) { a_mem(r->value, out); return 1; }
    }
    return 0;
}
static int a_is_cmp(Node *n) {
    return n && (n->type == NODE_EQ || n->type == NODE_NE || n->type == NODE_LT ||
                 n->type == NODE_GT || n->type == NODE_LE || n->type == NODE_GE);
}
static const char *a_cc(NodeType t) {
    switch (t) {
        case NODE_EQ: return "e";
        case NODE_NE: return "ne";
        case NODE_LT: return "l";
        case NODE_GT: return "g";
        case NODE_LE: return "le";
        default:      return "ge";
    }
}
/* Jump to label when cond is false. A comparison jumps directly (no 0/1 materialization). */
static void a_branch_false(Node *c, const char *label, FILE *o);
static void a_branch_false(Node *c, const char *label, FILE *o) {
    char src[96];
    if (a_is_cmp(c)) {
        if (a_cmp_src(c->right, src)) {
            gen_ia16(c->left, o);
            fprintf(o, "    cmpw %s, %%ax\n", src);
        } else {
            a_operands(c->left, c->right, o);
            fprintf(o, "    cmpw %%cx, %%ax\n");
        }
        a_jcc(a_cc(c->type), label, o);     /* skip the jmp while the condition holds */
    } else if (c->type == NODE_NOT) {
        gen_ia16(c->left, o);
        fprintf(o, "    testw %%ax, %%ax\n");
        a_jcc("z", label, o);
    } else if (c->type == NODE_AND) {
        a_branch_false(c->left, label, o);
        a_branch_false(c->right, label, o);
    } else if (c->type == NODE_IDENTIFIER && !a_is_std(c->value) && a_st(c->value) != 2) {
        char m[80]; a_mem(c->value, m);
        fprintf(o, a_st(c->value) == 1 ? "    cmpb $0, %s\n" : "    cmpw $0, %s\n", m);
        a_jcc("ne", label, o);
    } else {
        gen_ia16(c, o);
        fprintf(o, "    testw %%ax, %%ax\n");
        a_jcc("nz", label, o);
    }
}

/* %ax = (左 cc 右) ? 1 : 0 。cc は jcc の条件名 */
static void a_setcc(const char *cc, FILE *o) {
    int l = a_lbl++;
    fprintf(o, "    j%s .Lsy%d\n    xorw %%ax, %%ax\n    jmp .Lse%d\n.Lsy%d:\n    movw $1, %%ax\n.Lse%d:\n", cc, l, l, l, l);
}

static void a_cmp(Node *node, FILE *o) {
    char src[96];
    if (a_cmp_src(node->right, src)) {
        gen_ia16(node->left, o);
        fprintf(o, "    cmpw %s, %%ax\n", src);
    } else {
        a_operands(node->left, node->right, o);
        fprintf(o, "    cmpw %%cx, %%ax\n");
    }
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
    /* jcc rel8 は ±127 だが、ここは直後 2 命令を飛ばすだけなので届く */
    a_setcc(cc, o);
}

/* &a[i] / a[i] の要素アドレスを %ax へ。戻り値=要素サイズ。*ss=SS 側か */
static int a_elem_addr(Node *idxnode, FILE *o, int *ss) {
    Node *b = idxnode->left;
    int esz = idxnode->array_size;
    *ss = 0;
    if (b->type == NODE_IDENTIFIER) {
        int st = a_st(b->value);
        if (esz <= 0) esz = a_int_elem(b->value) ? 2 : 1;
        if (st == 3) { char m[80]; a_mem(b->value, m);
                       fprintf(o, "    movw %s, %%ax\n", m); }        /* ポインタ値(DS) */
        else         { a_addr_ax(b->value, o);                        /* 配列先頭 */
                       *ss = a_loc(b->value) >= 0; }
    } else {
        if (esz <= 0) {
            esz = 2;
            if (b->type == NODE_INDEX && b->left && b->left->type == NODE_IDENTIFIER
                && a_int_elem(b->left->value))
                esz = 1;
        }
        gen_ia16(b, o);
        *ss = a_is_ss(b);
    }
    if ((esz == 1 || esz == 2) && a_simple(idxnode->right)) {      /* base is in %ax already */
        if (idxnode->right->type == NODE_NUMBER)
            { long kk = strtol(idxnode->right->value, NULL, 0) * esz;
              if (kk) fprintf(o, "    addw $%ld, %%ax\n", kk); }
        else {
            a_load_cx(idxnode->right, o);
            if (esz == 2) fprintf(o, "    shlw $1, %%cx\n");
            fprintf(o, "    addw %%cx, %%ax\n");
        }
        return esz;
    }
    fprintf(o, "    pushw %%ax\n");
    gen_ia16(idxnode->right, o);
    if (esz == 2)      fprintf(o, "    shlw $1, %%ax\n");
    else if (esz == 4) fprintf(o, "    shlw $1, %%ax\n    shlw $1, %%ax\n");
    else if (esz != 1) fprintf(o, "    movw $%d, %%cx\n    imulw %%cx\n", esz);
    fprintf(o, "    popw %%cx\n    addw %%cx, %%ax\n");
    return esz;
}

/* ++/-- の一般形用: target のアドレスを %ax へ。*ss=SS 側か。要素サイズを返す */
static int a_lval_addr(Node *t, FILE *o, int *ss) {
    *ss = 0;
    if (t->type == NODE_INDEX) {
        return a_elem_addr(t, o, ss);
    } else if (t->type == NODE_MEMBER) {
        int save = t->base_type;
        t->base_type = 2;
        gen_ia16(t, o);
        t->base_type = save;
        *ss = (t->value && t->value[0] == '-') ? a_is_ss(t->left)
              : (t->left && t->left->type == NODE_IDENTIFIER ? a_loc(t->left->value) >= 0
                                                             : a_is_ss(t->left));
        return t->esz == 1 ? 1 : 2;
    } else if (t->type == NODE_IDENTIFIER) {
        int st = a_st(t->value);
        a_addr_ax(t->value, o);
        *ss = a_loc(t->value) >= 0;
        return (st == 1) ? 1 : 2;
    }
    fprintf(o, "    xorw %%ax, %%ax\n");
    return 2;
}

/* 1 つの式だけ(next を切り離して)生成 */
static void a_one(Node *a, FILE *o) {
    Node *sv = a->next; a->next = NULL;
    gen_ia16(a, o);
    a->next = sv;
}

/* ssp に対する %bx 経由の読み書き。%ax=アドレス */
static void a_ld(int esz, int ss, FILE *o) {
    fprintf(o, "    movw %%ax, %%bx\n");
    if (esz >= 2) fprintf(o, "    movw %s(%%bx), %%ax\n", a_seg(ss));
    else          fprintf(o, "    movb %s(%%bx), %%al\n    xorb %%ah, %%ah\n", a_seg(ss));
}
/* %ax=アドレス, %cx=値 */
static void a_st_cx(int esz, int ss, FILE *o) {
    fprintf(o, "    movw %%ax, %%bx\n");
    if (esz >= 2) fprintf(o, "    movw %%cx, %s(%%bx)\n", a_seg(ss));
    else          fprintf(o, "    movb %%cl, %s(%%bx)\n", a_seg(ss));
}

static void a_demote_long(Node *n) {
    if (!n) return;
    if ((n->type == NODE_VAR_DECL || n->type == NODE_ARRAY_DECL || n->type == NODE_PTR_DECL)
        && n->base_type == 2)
        n->base_type = 1;      /* PoC: long は未対応。int(16bit)扱い */
    a_demote_long(n->left);  a_demote_long(n->right);
    a_demote_long(n->third); a_demote_long(n->fourth);
    a_demote_long(n->next);
}

void gen_ia16(Node *node, FILE *o) {
    if (!node) return;
    switch (node->type) {
        case NODE_ROOT:
            a_root = node;
            fprintf(o, "    .text\n");
            gen_ia16(node->left, o);
            break;

        case NODE_FUNC: {
            const char *nm = node->value;
            Node *params = node->right ? node->left : NULL;
            Node *body   = node->right ? node->right : node->left;
            a_nl = 0; a_framesz = 0; a_fn = nm;
            a_collect(params);
            a_collect(body);
            int frame = (a_framesz + 1) & ~1;
            fprintf(o, "    .section .text.%s,\"ax\",@progbits\n", nm);   /* one section per function: --gc-sections can drop unused ones */
            fprintf(o, "    .globl %s\n%s:\n", nm, nm);
            fprintf(o, "    pushw %%bp\n    movw %%sp, %%bp\n");
            if (frame) fprintf(o, "    subw $%d, %%sp\n", frame);
            int i = 0;
            for (Node *p = params; p; p = p->next, i++) {
                char m[80]; a_mem(p->value, m);
                int st = (p->type == NODE_PTR_DECL) ? 3 : (p->base_type >= 1 ? 4 : 1);
                fprintf(o, "    movw %d(%%bp), %%ax\n", 4 + 2 * i);
                if (st == 1) fprintf(o, "    movb %%al, %s\n", m);
                else         fprintf(o, "    movw %%ax, %s\n", m);
            }
            if (body) gen_ia16(body, o);
            fprintf(o, "    xorw %%ax, %%ax\n    movw %%bp, %%sp\n    popw %%bp\n    ret\n");
            a_nl = 0;
            break;
        }

        case NODE_BLOCK:
            gen_ia16(node->left, o);
            break;

        case NODE_ARRAY_DECL: {
            int li = a_loc(node->value);
            if (li >= 0 && node->left && node->left->type == NODE_STRING && node->left->value) {
                int off = a_loff[li];
                int k = 0;
                for (const unsigned char *p = (const unsigned char *)node->left->value; ; p++, k++) {
                    fprintf(o, "    movb $%d, -%d(%%bp)\n", *p, off - k);
                    if (!*p) break;
                }
            }
            break;
        }

        case NODE_VAR_DECL: {
            if (node->left) {
                int dst = (node->base_type >= 1) ? 4 : 1;
                if (node->left->type == NODE_STRING) {
                    fprintf(o, "    movw $%d, %%ax\n", (unsigned char)node->left->value[0]);
                    a_store_scalar(node->value, dst, o);
                } else {
                    a_escape(node->left, "initializer");
                    gen_ia16(node->left, o);
                    a_store_scalar(node->value, dst, o);
                }
            }
            break;
        }

        case NODE_PTR_DECL:
            if (node->left) {
                if (node->left->type == NODE_STRING || node->left->type == NODE_ARG)
                    a_string(node->left, o);
                else {
                    a_escape(node->left, "initializer");
                    gen_ia16(node->left, o);
                }
                char m[80]; a_mem(node->value, m);
                fprintf(o, "    movw %%ax, %s\n", m);
            }
            break;

        case NODE_ASSIGN: {
            int st = a_st(node->value);
            if (!st) st = 4;
            if (node->left->type == NODE_NUMBER && st != 2) {
                char m[80]; a_mem(node->value, m);
                long v = strtol(node->left->value, NULL, 0);
                if (st == 1) fprintf(o, "    movb $%ld, %s\n", v & 0xFF, m);
                else         fprintf(o, "    movw $%ld, %s\n", v & 0xFFFF, m);
                break;
            }
            if (node->left->type == NODE_STRING || node->left->type == NODE_ARG)
                a_string(node->left, o);
            else {
                a_escape(node->left, "assignment");
                gen_ia16(node->left, o);
            }
            a_store_scalar(node->value, st, o);
            break;
        }

        case NODE_STORE_INDEX: {
            a_escape(node->third, "store value");
            {   /* constant index into a named array: one direct store */
                Node *b = node->left;
                int e = node->array_size;
                if (b->type == NODE_IDENTIFIER && a_st(b->value) == 2 && node->right->type == NODE_NUMBER) {
                    if (e <= 0) e = a_int_elem(b->value) ? 2 : 1;
                    if (e == 1 || e == 2) {
                        long k = strtol(node->right->value, NULL, 0) * e;
                        char ad[96];
                        if (a_loc(b->value) >= 0) sprintf(ad, "%ld(%%bp)", (long)(-a_loff[a_loc(b->value)]) + k);
                        else if (k) { sprintf(ad, "var_%s+%ld", b->value, k); a_ref(b->value); }
                        else { sprintf(ad, "var_%s", b->value); a_ref(b->value); }
                        if (node->third->type == NODE_NUMBER) {
                            long v = strtol(node->third->value, NULL, 0);
                            if (e == 1) fprintf(o, "    movb $%ld, %s\n", v & 0xFF, ad);
                            else        fprintf(o, "    movw $%ld, %s\n", v & 0xFFFF, ad);
                            break;
                        }
                        if (a_simple(node->third)) {
                            a_load_cx(node->third, o);
                            fprintf(o, e == 1 ? "    movb %%cl, %s\n" : "    movw %%cx, %s\n", ad);
                            break;
                        }
                    }
                }
            }
            if (a_simple(node->third)) {            /* address first, then the value straight into %cx */
                int ss;
                int esz = a_elem_addr(node, o, &ss);
                fprintf(o, "    movw %%ax, %%bx\n");
                a_load_cx(node->third, o);
                if (esz >= 2) fprintf(o, "    movw %%cx, %s(%%bx)\n", a_seg(ss));
                else          fprintf(o, "    movb %%cl, %s(%%bx)\n", a_seg(ss));
                break;
            }
            gen_ia16(node->third, o);
            fprintf(o, "    pushw %%ax\n");
            int ss;
            int esz = a_elem_addr(node, o, &ss);
            fprintf(o, "    popw %%cx\n");
            a_st_cx(esz, ss, o);
            break;
        }
        case NODE_INDEX: {
            int ss;
            int esz = a_elem_addr(node, o, &ss);
            if (node->base_type == 2) break;   /* 多次元途中: アドレスのまま */
            a_ld(esz, ss, o);
            break;
        }

        case NODE_DEREF: {
            gen_ia16(node->left, o);
            a_ld(1, a_is_ss(node->left) || a_is_ss_base(node->left), o);
            break;
        }
        case NODE_STORE_DEREF: {
            a_escape(node->right, "store value");
            gen_ia16(node->right, o);
            fprintf(o, "    pushw %%ax\n");
            gen_ia16(node->left, o);
            fprintf(o, "    popw %%cx\n");
            a_st_cx(1, a_is_ss(node->left) || a_is_ss_base(node->left), o);
            break;
        }
        case NODE_ADDR:
            if (node->left && node->left->type == NODE_IDENTIFIER)
                a_addr_ax(node->left->value, o);
            else if (node->left && node->left->type == NODE_INDEX) {
                int ss; a_elem_addr(node->left, o, &ss);
            } else if (node->left && node->left->type == NODE_MEMBER) {
                int save = node->left->base_type;
                node->left->base_type = 2;
                gen_ia16(node->left, o);
                node->left->base_type = save;
            } else
                fprintf(o, "    xorw %%ax, %%ax\n");
            break;

        case NODE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            int ss = 0;
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) { char m[80]; a_mem(b->value, m);
                             fprintf(o, "    movw %s, %%ax\n", m); }
                else       { a_addr_ax(b->value, o); ss = a_loc(b->value) >= 0; }
            } else {
                gen_ia16(b, o);
                ss = a_is_ss(b);
            }
            if (node->array_size) fprintf(o, "    addw $%d, %%ax\n", node->array_size);
            if (node->base_type == 2) break;   /* 配列メンバ: アドレスを返す */
            a_ld(node->esz == 1 ? 1 : 2, ss, o);
            break;
        }
        case NODE_STORE_MEMBER: {
            Node *b = node->left;
            int arrow = node->value && node->value[0] == '-';
            int ss = 0;
            a_escape(node->right, "store value");
            gen_ia16(node->right, o);
            fprintf(o, "    pushw %%ax\n");
            if (b->type == NODE_IDENTIFIER) {
                if (arrow) { char m[80]; a_mem(b->value, m);
                             fprintf(o, "    movw %s, %%ax\n", m); }
                else       { a_addr_ax(b->value, o); ss = a_loc(b->value) >= 0; }
            } else {
                gen_ia16(b, o);
                ss = a_is_ss(b);
            }
            if (node->array_size) fprintf(o, "    addw $%d, %%ax\n", node->array_size);
            fprintf(o, "    popw %%cx\n");
            a_st_cx(node->esz == 1 ? 1 : 2, ss, o);
            break;
        }

        case NODE_CALL: {
            int argc = 0;
            Node *args[64];
            for (Node *a = node->left; a && argc < 64; a = a->next) args[argc++] = a;
            for (int i = argc - 1; i >= 0; i--) {
                Node *a = args[i];
                if (a->type == NODE_STRING || a->type == NODE_ARG) a_string(a, o);
                else if (a->type == NODE_IDENTIFIER && a_is_std(a->value))
                    fprintf(o, "    movw %s, %%ax\n", a->value);
                else {
                    a_escape(a, "call argument");
                    a_one(a, o);
                }
                fprintf(o, "    pushw %%ax\n");
            }
            const char *cn = node->value;
            if (strcmp(cn, "putc") == 0) cn = "putchar";
            fprintf(o, "    call %s\n", cn);
            if (ia16_is_narrowfunc(node->value))          /* declared char: the callee may leave AH undefined */
                fprintf(o, "    xorb %%ah, %%ah\n");
            if (argc) fprintf(o, "    addw $%d, %%sp\n", 2 * argc);
            break;
        }

        case NODE_RETURN:
            if (node->left) {
                a_escape(node->left, "return value");
                gen_ia16(node->left, o);
            }
            fprintf(o, "    movw %%bp, %%sp\n    popw %%bp\n    ret\n");
            break;

        case NODE_NUMBER:
            fprintf(o, "    movw $%s, %%ax\n", node->value);
            break;

        case NODE_STRING:
            a_string(node, o);
            break;

        case NODE_IDENTIFIER: {
            if (a_is_std(node->value)) { fprintf(o, "    movw %s, %%ax\n", node->value); break; }
            int st = a_st(node->value);
            if (st == 2)      a_addr_ax(node->value, o);
            else if (st == 1) a_load_scalar(node->value, 1, o);
            else { char m[80]; a_mem(node->value, m);
                   fprintf(o, "    movw %s, %%ax\n", m); }
            break;
        }

        case NODE_ADD: case NODE_SUB: case NODE_MUL: case NODE_DIV: case NODE_IA16_MOD: {
            if (node->type == NODE_ADD && node->right->type == NODE_NUMBER) {
                gen_ia16(node->left, o);
                fprintf(o, "    addw $%s, %%ax\n", node->right->value);
                break;
            }
            a_operands(node->left, node->right, o);
            if (node->type == NODE_ADD) fprintf(o, "    addw %%cx, %%ax\n");
            else if (node->type == NODE_SUB) fprintf(o, "    subw %%cx, %%ax\n");
            else if (node->type == NODE_MUL) fprintf(o, "    imulw %%cx\n");
            else if (node->type == NODE_DIV)
                fprintf(o, "    cwtd\n    idivw %%cx\n");
            else   /* %: idiv leaves the remainder in DX */
                fprintf(o, "    cwtd\n    idivw %%cx\n    movw %%dx, %%ax\n");
            break;
        }

        case NODE_NEG:
            gen_ia16(node->left, o);
            fprintf(o, "    negw %%ax\n");
            break;
        case NODE_BITNOT:
            gen_ia16(node->left, o);
            fprintf(o, "    notw %%ax\n");
            break;
        case NODE_NOT:
            gen_ia16(node->left, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_setcc("e", o);
            break;

        case NODE_BITAND: case NODE_BITOR: case NODE_BITXOR: {
            a_operands(node->left, node->right, o);
            fprintf(o, node->type == NODE_BITAND ? "    andw %%cx, %%ax\n" :
                       node->type == NODE_BITOR  ? "    orw %%cx, %%ax\n"  :
                                                   "    xorw %%cx, %%ax\n");
            break;
        }
        case NODE_SHL: case NODE_SHR: {
            a_operands(node->left, node->right, o);
            fprintf(o, node->type == NODE_SHL ? "    shlw %%cl, %%ax\n" : "    shrw %%cl, %%ax\n");
            break;
        }

        case NODE_EQ: case NODE_NE: case NODE_LT: case NODE_GT: case NODE_LE: case NODE_GE:
            a_cmp(node, o);
            break;

        case NODE_AND: {
            int l = a_lbl++;
            char lf[32];
            sprintf(lf, ".Laf%d", l);
            gen_ia16(node->left, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_jcc("nz", lf, o);
            gen_ia16(node->right, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_jcc("nz", lf, o);
            fprintf(o, "    movw $1, %%ax\n    jmp .Lae%d\n%s:\n    xorw %%ax, %%ax\n.Lae%d:\n", l, lf, l);
            break;
        }
        case NODE_OR: {
            int l = a_lbl++;
            char lt[32];
            sprintf(lt, ".Lot%d", l);
            gen_ia16(node->left, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_jcc("z", lt, o);
            gen_ia16(node->right, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_jcc("z", lt, o);
            fprintf(o, "    xorw %%ax, %%ax\n    jmp .Loe%d\n%s:\n    movw $1, %%ax\n.Loe%d:\n", l, lt, l);
            break;
        }
        case NODE_TERNARY: {
            int l = a_lbl++;
            char lf[32], le[32];
            sprintf(lf, ".Ltf%d", l); sprintf(le, ".Lte%d", l);
            gen_ia16(node->left, o);
            fprintf(o, "    testw %%ax, %%ax\n");
            a_jcc("nz", lf, o);
            gen_ia16(node->right, o);
            fprintf(o, "    jmp %s\n%s:\n", le, lf);
            gen_ia16(node->third, o);
            fprintf(o, "%s:\n", le);
            break;
        }

        case NODE_PREINC: case NODE_PREDEC: {
            if (node->value) {
                int st = a_st(node->value); if (!st) st = 4;
                a_load_scalar(node->value, st, o);
                fprintf(o, node->type == NODE_PREINC ? "    incw %%ax\n" : "    decw %%ax\n");
                a_store_scalar(node->value, st, o);
            } else {
                int ss;
                int esz = a_lval_addr(node->left, o, &ss);
                fprintf(o, "    movw %%ax, %%bx\n    pushw %%bx\n");
                if (esz >= 2) fprintf(o, "    movw %s(%%bx), %%ax\n", a_seg(ss));
                else          fprintf(o, "    movb %s(%%bx), %%al\n    xorb %%ah, %%ah\n", a_seg(ss));
                fprintf(o, node->type == NODE_PREINC ? "    incw %%ax\n" : "    decw %%ax\n");
                fprintf(o, "    popw %%bx\n");
                if (esz >= 2) fprintf(o, "    movw %%ax, %s(%%bx)\n", a_seg(ss));
                else          fprintf(o, "    movb %%al, %s(%%bx)\n", a_seg(ss));
            }
            break;
        }
        case NODE_POSTINC: case NODE_POSTDEC: {
            if (node->value) {
                int st = a_st(node->value); if (!st) st = 4;
                a_load_scalar(node->value, st, o);
                fprintf(o, "    pushw %%ax\n");
                fprintf(o, node->type == NODE_POSTINC ? "    incw %%ax\n" : "    decw %%ax\n");
                a_store_scalar(node->value, st, o);
                fprintf(o, "    popw %%ax\n");
            } else {
                int ss;
                int esz = a_lval_addr(node->left, o, &ss);
                fprintf(o, "    movw %%ax, %%bx\n");
                if (esz >= 2) fprintf(o, "    movw %s(%%bx), %%ax\n", a_seg(ss));
                else          fprintf(o, "    movb %s(%%bx), %%al\n    xorb %%ah, %%ah\n", a_seg(ss));
                fprintf(o, "    movw %%ax, %%cx\n");
                fprintf(o, node->type == NODE_POSTINC ? "    incw %%cx\n" : "    decw %%cx\n");
                if (esz >= 2) fprintf(o, "    movw %%cx, %s(%%bx)\n", a_seg(ss));
                else          fprintf(o, "    movb %%cl, %s(%%bx)\n", a_seg(ss));
            }
            break;
        }

        case NODE_IF: {
            int l = a_lbl++;
            char le[32], ld[32];
            sprintf(le, ".Lelse%d", l); sprintf(ld, ".Lend%d", l);
            a_branch_false(node->left, node->third ? le : ld, o);
            if (node->third) {
                if (node->right) gen_ia16(node->right, o);
                fprintf(o, "    jmp %s\n%s:\n", ld, le);
                gen_ia16(node->third, o);
                fprintf(o, "%s:\n", ld);
            } else {
                if (node->right) gen_ia16(node->right, o);
                fprintf(o, "%s:\n", ld);
            }
            break;
        }
        case NODE_WHILE: {
            int l = a_lbl++;
            char lb[32];
            sprintf(lb, ".Lbrk%d", l);
            a_cont[a_cont_sp++] = l; a_brk[a_brk_sp++] = l;
            fprintf(o, ".Lcont%d:\n", l);
            a_branch_false(node->left, lb, o);
            if (node->right) gen_ia16(node->right, o);
            fprintf(o, "    jmp .Lcont%d\n.Lbrk%d:\n", l, l);
            a_cont_sp--; a_brk_sp--;
            break;
        }
        case NODE_FOR: {
            int l = a_lbl++;
            char lb[32];
            sprintf(lb, ".Lbrk%d", l);
            a_cont[a_cont_sp++] = l; a_brk[a_brk_sp++] = l;
            if (node->left) gen_ia16(node->left, o);
            fprintf(o, ".Lbeg%d:\n", l);
            if (node->right)
                a_branch_false(node->right, lb, o);
            if (node->fourth) gen_ia16(node->fourth, o);
            fprintf(o, ".Lcont%d:\n", l);
            if (node->third) gen_ia16(node->third, o);
            fprintf(o, "    jmp .Lbeg%d\n.Lbrk%d:\n", l, l);
            a_cont_sp--; a_brk_sp--;
            break;
        }
        case NODE_BREAK:
            if (a_brk_sp) fprintf(o, "    jmp .Lbrk%d\n", a_brk[a_brk_sp - 1]);
            break;
        case NODE_CONTINUE:
            if (a_cont_sp) fprintf(o, "    jmp .Lcont%d\n", a_cont[a_cont_sp - 1]);
            break;

        case NODE_CASE:
            fprintf(o, ".Lcase%d_%d:\n", node->array_size, node->base_type);
            break;
        case NODE_DEFAULT:
            fprintf(o, ".Ldef%d:\n", node->array_size);
            break;
        case NODE_SWITCH: {
            int l = a_lbl++;
            a_brk[a_brk_sp++] = l;
            gen_ia16(node->left, o);
            Node *b = node->right ? node->right->left : NULL;
            int has_def = 0;
            int ci = 0;
            for (Node *p = b; p; p = p->next) {
                if (p->type == NODE_CASE && p->value) {
                    char lc[48];
                    p->array_size = l; p->base_type = ci;
                    sprintf(lc, ".Lcase%d_%d", l, ci);
                    fprintf(o, "    cmpw $%s, %%ax\n", p->value);
                    a_jcc("ne", lc, o);
                    ci++;
                } else if (p->type == NODE_DEFAULT) { p->array_size = l; has_def = 1; }
            }
            fprintf(o, has_def ? "    jmp .Ldef%d\n" : "    jmp .Lbrk%d\n", l);
            for (Node *p = b; p; p = p->next) {
                Node *sv = p->next; p->next = NULL;
                gen_ia16(p, o);
                p->next = sv;
            }
            fprintf(o, ".Lbrk%d:\n", l);
            a_brk_sp--;
            break;
        }

        case NODE_CVT:
            gen_ia16(node->left, o);
            break;

        default:
            gen_ia16(node->left, o);
            break;
    }
    gen_ia16(node->next, o);
}

/* ---- データ領域 ---- */
static char *ad_seen[1024]; static int ad_seen_n = 0;
static int ad_dup(const char *name) {
    for (int i = 0; i < ad_seen_n; i++) if (strcmp(ad_seen[i], name) == 0) return 1;
    if (ad_seen_n < 1024) ad_seen[ad_seen_n++] = strdup(name);
    return 0;
}

static void a_emit_data(Node *node, FILE *o) {
    if (!node) return;
    if ((node->type == NODE_VAR_DECL || node->type == NODE_PTR_DECL || node->type == NODE_ARRAY_DECL)
        && node->value && (!a_is_ref(node->value) || ad_dup(node->value))) {
        a_emit_data(node->left, o);  a_emit_data(node->right, o);
        a_emit_data(node->third, o); a_emit_data(node->fourth, o);
        a_emit_data(node->next, o);
        return;
    }
    if (node->type == NODE_VAR_DECL) {
        fprintf(o, "    .data\nvar_%s:\n", node->value);
        fprintf(o, node->base_type >= 1 ? "    .word 0\n" : "    .byte 0\n");
    } else if (node->type == NODE_PTR_DECL) {
        fprintf(o, "    .data\nvar_%s:\n    .word 0\n", node->value);
    } else if (node->type == NODE_ARRAY_DECL) {
        int n = node->array_size > 0 ? node->array_size : 1;
        if (node->left && node->left->type == NODE_STRING && node->left->value) {
            const char *s = node->left->value;
            int slen = (int)strlen(s);
            fprintf(o, "    .data\nvar_%s:\n    .byte ", node->value);
            for (const unsigned char *p = (const unsigned char *)s; ; p++) {
                fprintf(o, "%d%s", *p, *p ? "," : ""); if (!*p) break;
            }
            fprintf(o, "\n");
            if (n > slen + 1) fprintf(o, "    .zero %d\n", n - (slen + 1));
        } else if (node->left && node->left->type == NODE_INITLIST) {
            int esz = (node->base_type >= 1) ? 2 : 1;
            int cnt = 0;
            fprintf(o, "    .data\nvar_%s:\n", node->value);
            for (Node *e = node->left->left; e; e = e->next) {
                long v = (e->type == NODE_NUMBER) ? strtol(e->value, NULL, 0) : 0;
                fprintf(o, esz == 1 ? "    .byte %ld\n" : "    .word %ld\n", v);
                cnt++;
            }
            if (n > cnt) fprintf(o, "    .zero %d\n", (n - cnt) * esz);
        } else {
            if (node->base_type >= 1) n *= 2;
            fprintf(o, "    .bss\nvar_%s:\n    .zero %d\n", node->value, n);
        }
    }
    if (node->type != NODE_ARRAY_DECL) a_emit_data(node->left, o);
    a_emit_data(node->right, o);
    a_emit_data(node->third, o);
    a_emit_data(node->fourth, o);
    a_emit_data(node->next, o);
}

/* ---- peephole: runs over the finished text, one pass per rule set until nothing changes ---- */
#define A_MAXLINES 400000
static char **pl; static int pn;
static int a_peep_once(void) {
    int changed = 0, w = 0;
    char **o2 = malloc(sizeof(char *) * (size_t)(pn + 1));
    for (int i = 0; i < pn; i++) {
        const char *a = pl[i], *b = i + 1 < pn ? pl[i + 1] : "";
        char x[96], y[96];
        int nb = -1;
        /* movw %ax, X ; movw X, %ax  ->  movw %ax, X
         * (%n: the second line must end in "%ax" -- sscanf counts the conversion even when the literal after it
         *  does not match, so "movw X, %cx" used to be dropped too) */
        if (sscanf(a, " movw %%ax, %95[^\n]", x) == 1 && sscanf(b, " movw %95[^,], %%ax%n", y, &nb) == 1 &&
            nb == (int)strlen(b) && strcmp(x, y) == 0 && strcmp(x, "%cx") && strcmp(x, "%bx")) {
            o2[w++] = pl[i]; i++; changed = 1; continue;
        }
        /* pushw %ax ; popw %ax */
        if (!strcmp(a, "    pushw %ax") && !strcmp(b, "    popw %ax")) { i++; changed = 1; continue; }
        /* jmp L ; L: */
        if (sscanf(a, " jmp %95s", x) == 1 && b[0] != ' ' && b[0] != '#') {
            char lab[96]; snprintf(lab, sizeof lab, "%s:", x);
            if (strcmp(b, lab) == 0) { changed = 1; continue; }
        }
        /* movw $0, %ax  ->  xorw %ax, %ax  (not before a flag test that follows a cmp: none is generated that way) */
        if (!strcmp(a, "    movw $0, %ax")) { o2[w++] = "    xorw %ax, %ax"; changed = 1; continue; }
        o2[w++] = pl[i];
    }
    for (int i = 0; i < w; i++) pl[i] = o2[i];
    pn = w; free(o2);
    return changed;
}
static void a_peephole(char *text, FILE *out) {
    pl = malloc(sizeof(char *) * A_MAXLINES); pn = 0;
    for (char *p = text; *p && pn < A_MAXLINES; ) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        pl[pn++] = p;
        if (!e) break;
        p = e + 1;
    }
    while (a_peep_once()) ;
    for (int i = 0; i < pn; i++) fprintf(out, "%s\n", pl[i]);
    free(pl);
}

void generate_ia16(Node *root, FILE *out) {
    char *buf = NULL; size_t blen = 0;
    FILE *mem = open_memstream(&buf, &blen);
    fprintf(mem, "# Generated by tzcc (x86 16bit / ia16 backend, PoC)\n");
    fprintf(mem, "    .code16\n");
    a_demote_long(root);
    char *cbuf = NULL; size_t clen = 0;
    FILE *cmem = open_memstream(&cbuf, &clen);
    a_root = root;
    gen_ia16(root->left, cmem);         /* code first: it records the globals it refers to */
    fclose(cmem);
    a_emit_data(root->left, mem);
    fprintf(mem, "    .text\n");
    fwrite(cbuf, 1, clen, mem);
    free(cbuf);
    fclose(mem);
    a_peephole(buf, out);
    free(buf);
    if (a_escapes)
        fprintf(stderr, "ia16: SS pointer escapes: %d site(s)\n", a_escapes);
}
