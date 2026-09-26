extern int g_dupcheck_fatal;   /* generator.c: 関数間の同名ローカル検出を致命化 (tizix #31) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "generator.h"
#include "tizix.h"

// 現在のトークンを保持
static Token cur_token;
static int has_error = 0;
static const char *filename = "";

// 次のトークンを取得して更新
static void next_token() {
    Token *t = lexer_next_token();
    cur_token.type    = t->type;
    cur_token.value   = t->value;
    cur_token.line    = t->line;
    cur_token.is_long = t->is_long;
    if (cur_token.type == TOKEN_UNKNOWN) {
        fprintf(stderr, "%s:%d: error: unknown token '%c'\n", filename, cur_token.line, lexer_get_last_error_char());
        has_error = 1;
    }
}

/* ------------------------------------------------------------------ */
/* struct 型テーブル（サブセット: 全メンバ 2 バイト固定）              */
/* ------------------------------------------------------------------ */
#define MAX_STRUCTS 64
#define MAX_MEMBERS 48

/* 基本整数/ポインタのバイト幅。x86 バックエンドでは 8、それ以外(Z80)は 2。
   構造体レイアウトを正しく出すため main() で --march=x86 判定してセットする。 */
static int g_intsz = 2;

typedef struct {
    char name[64];                       /* タグ名 or typedef 別名（複数エイリアスは別エントリ） */
    int  self;                           /* 同一 struct を指すエイリアス群の代表 index */
    char *memb[MAX_MEMBERS];             /* strdup 済みメンバ名（tzcc は 2次元配列を出せない） */
    int  memb_isptr[MAX_MEMBERS];        /* そのメンバがポインタ型か（char* を含む） */
    int  memb_charptr[MAX_MEMBERS];      /* char* ポインタメンバか（p->m[i] のストライド判定用） */
    int  memb_struct[MAX_MEMBERS];       /* ポインタメンバが指す struct index（無ければ -1） */
    int  memb_off[MAX_MEMBERS];          /* メンバのバイトオフセット */
    int  memb_esz[MAX_MEMBERS];          /* メンバの（最内）要素バイトサイズ */
    int  memb_ndim[MAX_MEMBERS];         /* 配列次元数（0=スカラ, 1=1D, 2=2D） */
    int  memb_d1[MAX_MEMBERS];           /* 2D のとき 2 番目の次元数（行サイズ算出用） */
    int  nmemb;
    int  size;                           /* struct 全体のバイトサイズ */
} StructType;

static StructType g_structs[MAX_STRUCTS];
static int g_nstructs = 0;

/* 変数名 -> struct 型 index */
static struct { char var[64]; int sidx; } g_svars[512];
static int g_nsvars = 0;

static int struct_by_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_nstructs; i++)
        if (strcmp(g_structs[i].name, name) == 0) return g_structs[i].self;
    return -1;
}
static int member_index(int sidx, const char *m) {
    if (sidx < 0 || sidx >= g_nstructs) return -1;
    StructType *s = &g_structs[sidx];
    for (int i = 0; i < s->nmemb; i++)
        if (strcmp(s->memb[i], m) == 0) return i;
    return -1;
}
static void svar_register(const char *var, int sidx) {
    if (sidx < 0 || g_nsvars >= 512) return;
    strncpy(g_svars[g_nsvars].var, var, 63);
    g_svars[g_nsvars].var[63] = 0;
    g_svars[g_nsvars].sidx = sidx;
    g_nsvars++;
}
static int svar_struct(const char *var) {
    for (int i = g_nsvars - 1; i >= 0; i--)
        if (strcmp(g_svars[i].var, var) == 0) return g_svars[i].sidx;
    return -1;
}

/* typedef された単純別名（int 相当として扱う型名）。struct/enum tag とは別管理。
   tzcc は 2次元配列を出せないため strdup 済みポインタ配列で保持する。 */
static char *g_tnames[128];
static int   g_ntnames = 0;
static void tname_register(const char *n) {
    if (!n || !n[0] || g_ntnames >= 128) return;
    for (int i = 0; i < g_ntnames; i++) if (strcmp(g_tnames[i], n) == 0) return;
    g_tnames[g_ntnames] = strdup(n); g_ntnames++;
}
static int is_tname(const char *n) {
    if (!n) return 0;
    for (int i = 0; i < g_ntnames; i++) if (strcmp(g_tnames[i], n) == 0) return 1;
    return 0;
}

/* 戻り値が long(32bit) の関数名。定義・プロトタイプ両方で登録し、generator.c が
   `x = f()` の 32bit 受け取り判定に使う（プロトタイプは AST に残らないため）。*/
static char *g_longfuncs[128];
static int   g_nlongfuncs = 0;
static void longfunc_register(const char *n) {
    if (!n || !n[0] || g_nlongfuncs >= 128) return;
    for (int i = 0; i < g_nlongfuncs; i++) if (strcmp(g_longfuncs[i], n) == 0) return;
    g_longfuncs[g_nlongfuncs++] = strdup(n);
}
int func_returns_long(const char *n) {   /* generator.c から extern 参照 */
    if (!n) return 0;
    for (int i = 0; i < g_nlongfuncs; i++) if (strcmp(g_longfuncs[i], n) == 0) return 1;
    return 0;
}

// プロトタイプ宣言
static Node* parse_expr();
static Node* parse_primary();
static Node* parse_unary();
static Node* parse_stmt();
static Node* parse_block_or_stmt();
static Node* parse_simple_stmt_nosemi();

// 型トークンの連なり（char/int/void/unsigned/signed/long/short…）を消費する。
// 先頭が型トークンである前提。
// 戻り値: 0 = char幅(1byte) / 1 = int幅(2byte) / 2 = long幅(4byte)
static int parse_type_run() {
    int saw_char = 0, saw_wide = 0, saw_long = 0;
    while (cur_token.type == TOKEN_CHAR || cur_token.type == TOKEN_INT
        || cur_token.type == TOKEN_LONG
        || cur_token.type == TOKEN_VOID || cur_token.type == TOKEN_SIGN) {
        if (cur_token.type == TOKEN_CHAR) saw_char = 1;
        if (cur_token.type == TOKEN_INT)  saw_wide = 1;
        if (cur_token.type == TOKEN_LONG) saw_long = 1;
        next_token();
    }
    if (saw_long) return 2;
    return (saw_wide || !saw_char) ? 1 : 0;
}

// enum [tag] { NAME [= NUM], ... } [alias] ;
//   列挙子を数値マクロとして lexer に登録する。型としては int 相当（別途扱わない）。
static void parse_enum_decl() {
    next_token(); // consume 'enum'
    if (cur_token.type == TOKEN_IDENTIFIER) next_token(); // 省略可能なタグ
    if (cur_token.type != TOKEN_LBRACE) {
        // `enum Tag var;` 等（列挙定義でない使い方）は ; まで読み飛ばす
        while (cur_token.type != TOKEN_SEMICOLON && cur_token.type != TOKEN_EOF) next_token();
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return;
    }
    next_token(); // consume {
    int val = 0;
    while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
        if (cur_token.type == TOKEN_IDENTIFIER) {
            char nm[128];
            strcpy(nm, cur_token.value);
            next_token();
            if (cur_token.type == TOKEN_ASSIGN) {
                next_token();
                if (cur_token.type == TOKEN_NUMBER) {
                    val = atoi(cur_token.value);
                    next_token();
                }
            }
            char vb[16];
            sprintf(vb, "%d", val);
            lexer_define_macro(nm, vb);
            val++;
        } else {
            next_token();
        }
        if (cur_token.type == TOKEN_COMMA) next_token();
        else break;
    }
    if (cur_token.type == TOKEN_RBRACE) next_token();
    if (cur_token.type == TOKEN_IDENTIFIER) { tname_register(cur_token.value); next_token(); } // typedef enum {...} Alias;
    if (cur_token.type == TOKEN_SEMICOLON) next_token();
}

// struct/union [tag] { members } [alias] ;
//   is_typedef=1: 末尾の識別子は typedef 別名として消費し -1 を返す。
//   is_typedef=0: 末尾の識別子/宣言子は消費せず、定義した struct の sidx を返す
//                 （呼び出し側が `struct {..} var[..];` の変数宣言子を処理する）。
static int parse_struct_decl(int is_typedef) {
    next_token(); // consume 'struct'/'union'
    char tag[64] = "";
    if (cur_token.type == TOKEN_IDENTIFIER) { strncpy(tag, cur_token.value, 63); tag[63]=0; next_token(); }

    if (cur_token.type != TOKEN_LBRACE) {
        // `struct Tag *p;` 等。既知タグなら sidx を返す。
        return tag[0] ? struct_by_name(tag) : -1;
    }
    next_token(); // consume {

    int si = g_nstructs < MAX_STRUCTS ? g_nstructs++ : g_nstructs - 1;
    StructType *s = &g_structs[si];
    memset(s, 0, sizeof(*s));
    s->self = si;
    if (tag[0]) strncpy(s->name, tag, 63);   // 自己参照 struct Node* 用に早期登録

    int cur_off = 0;
    while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
        int line_ptr = 0;
        int line_struct = -1;   /* この宣言行の型が指す struct index */
        int line_char = 0;      /* この宣言行の基底型が char か */
        int line_long = 0;      /* この宣言行の基底型が long か */
        while (cur_token.type != TOKEN_SEMICOLON && cur_token.type != TOKEN_RBRACE
               && cur_token.type != TOKEN_EOF) {
            if (cur_token.type == TOKEN_STAR) { line_ptr = 1; next_token(); continue; }
            if (cur_token.type == TOKEN_STRUCT) {
                next_token();
                if (cur_token.type == TOKEN_IDENTIFIER) {
                    line_struct = struct_by_name(cur_token.value);
                    next_token();
                }
                continue;
            }
            if (cur_token.type == TOKEN_CHAR) { line_char = 1; next_token(); continue; }
            if (cur_token.type == TOKEN_LONG) { line_long = 1; next_token(); continue; }
            if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_VOID
                || cur_token.type == TOKEN_ENUM || cur_token.type == TOKEN_SIGN) { next_token(); continue; }
            if (cur_token.type == TOKEN_IDENTIFIER) {
                int ts = struct_by_name(cur_token.value);
                char nm[48]; strncpy(nm, cur_token.value, 47); nm[47]=0;
                next_token();
                int isptr = line_ptr;
                while (cur_token.type == TOKEN_STAR) { isptr = 1; next_token(); }
                if (cur_token.type == TOKEN_SEMICOLON || cur_token.type == TOKEN_COMMA
                    || cur_token.type == TOKEN_LBRACKET) {
                    // 配列次元を読む（[N] / [N][M]）
                    int dims[4]; int ndim = 0;   /* 各要素は使用前に必ず代入される */
                    while (cur_token.type == TOKEN_LBRACKET) {
                        next_token();
                        int d = 0;
                        if (cur_token.type == TOKEN_NUMBER) { d = atoi(cur_token.value); next_token(); }
                        if (cur_token.type == TOKEN_RBRACKET) next_token();
                        if (ndim < 4) dims[ndim++] = d;
                    }
                    int count = 1;
                    for (int k = 0; k < ndim; k++) count *= (dims[k] > 0 ? dims[k] : 1);
                    // 要素バイトサイズ
                    int esz;
                    if (isptr) esz = g_intsz;
                    else if (line_struct >= 0) esz = g_structs[line_struct].size;
                    else if (line_char) esz = 1;
                    else if (line_long) esz = (g_intsz >= 4) ? g_intsz : 4;  // long: z80=4 / x86=8
                    else esz = g_intsz;
                    if (s->nmemb < MAX_MEMBERS) {
                        s->memb[s->nmemb] = strdup(nm);
                        s->memb_isptr[s->nmemb] = isptr;
                        s->memb_charptr[s->nmemb] = (isptr && line_char) ? 1 : 0;
                        s->memb_struct[s->nmemb] = isptr ? line_struct : -1;
                        s->memb_off[s->nmemb] = cur_off;
                        s->memb_esz[s->nmemb] = esz;
                        s->memb_ndim[s->nmemb] = ndim;
                        s->memb_d1[s->nmemb] = ndim >= 2 ? (dims[1] > 0 ? dims[1] : 1) : 0;
                        s->nmemb++;
                    }
                    cur_off += esz * count;
                    line_ptr = 0;
                    if (cur_token.type == TOKEN_COMMA) { next_token(); continue; }
                    break;
                }
                // 型名の一部だった（例: NodeType type; / Node *next;）
                if (ts >= 0) line_struct = ts;
                continue;
            }
            next_token();
        }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
    }
    if (cur_token.type == TOKEN_RBRACE) next_token();
    s->size = cur_off > 0 ? cur_off : g_intsz;

    char alias[64] = "";
    if (is_typedef && cur_token.type == TOKEN_IDENTIFIER) {
        strncpy(alias, cur_token.value, 63); alias[63]=0; next_token();
    }
    if (is_typedef && cur_token.type == TOKEN_SEMICOLON) next_token();

    if (alias[0] && strcmp(alias, s->name) != 0 && g_nstructs < MAX_STRUCTS) {
        StructType *a = &g_structs[g_nstructs++];
        memset(a, 0, sizeof(*a));
        strncpy(a->name, alias, 63);
        a->self = si;
    }
    return is_typedef ? -1 : si;
}

// base_id の後、cur が -> か . のとき呼ぶ。メンバアクセスの連鎖を
// NODE_MEMBER のネスト（最内が IDENTIFIER）で返す。連鎖の途中で型が
// 追えなくなったら以降のオフセットは 0。
// base（式ノード, struct index=sidx）から始めて  ->m / .m / [i]  の連鎖を組む。
//   member が配列型なら NODE_MEMBER は「アドレスを返す」モード(base_type=2)にし、
//   続く [i] を NODE_INDEX（stride ベイク）で処理する。
static Node* parse_member_chain_from(Node *base, int sidx) {
    Node *node = base;
    int last_charptr = 0;   /* 直前の -> / . が char* メンバなら 1（m[i] のストライド判定用） */
    while (cur_token.type == TOKEN_ARROW || cur_token.type == TOKEN_DOT
           || cur_token.type == TOKEN_LBRACKET) {
        if (cur_token.type == TOKEN_LBRACKET) {
            // ポインタ変数 or 直前結果に対する添字（stride は不明なら 0＝後方でintと判定）
            next_token();
            Node *idx = parse_expr();
            if (cur_token.type == TOKEN_RBRACKET) next_token();
            Node *ix = new_node(NODE_INDEX, NULL);
            ix->left = node;
            ix->right = idx;
            if (last_charptr) ix->array_size = 1;   /* p->charptr[i] は 1 バイト刻み */
            node = ix;
            sidx = -1;
            last_charptr = 0;
            continue;
        }
        int is_arrow = (cur_token.type == TOKEN_ARROW);
        next_token();
        char mem[64] = "";
        if (cur_token.type == TOKEN_IDENTIFIER) { strncpy(mem, cur_token.value, 63); mem[63]=0; next_token(); }
        int mi = member_index(sidx, mem);
        int off  = (mi >= 0) ? g_structs[sidx].memb_off[mi]  : 0;
        int ndim = (mi >= 0) ? g_structs[sidx].memb_ndim[mi] : 0;
        int esz  = (mi >= 0) ? g_structs[sidx].memb_esz[mi]  : g_intsz;
        int d1   = (mi >= 0) ? g_structs[sidx].memb_d1[mi]   : 0;
        int nextstruct = (mi >= 0) ? g_structs[sidx].memb_struct[mi] : -1;
        last_charptr = (mi >= 0) ? g_structs[sidx].memb_charptr[mi] : 0;

        Node *m = new_node(NODE_MEMBER, strdup(is_arrow ? "->" : "."));
        if (is_arrow) m->left = node;
        else { Node *ad = new_node(NODE_ADDR, NULL); ad->left = node; m->left = ad; }
        m->array_size = off;
        m->base_type  = (ndim > 0) ? 2 : 1;    // 配列メンバはアドレスを返す
        m->esz        = esz;                    // スカラーメンバの実バイトサイズ(1/2/4)
        node = m;
        sidx = nextstruct;

        // 配列メンバへの添字  m[i]  /  m[i][j]
        int rem = ndim;
        while (rem > 0 && cur_token.type == TOKEN_LBRACKET) {
            next_token();
            Node *idx = parse_expr();
            if (cur_token.type == TOKEN_RBRACKET) next_token();
            Node *ix = new_node(NODE_INDEX, NULL);
            ix->left = node;
            ix->right = idx;
            // stride: 残り次元が 2 なら「行サイズ」= d1*esz、1 なら esz
            ix->array_size = (rem >= 2) ? d1 * esz : esz;
            ix->base_type  = (rem >= 2) ? 2 : 1;   // まだ配列ならアドレス継続
            node = ix;
            rem--;
        }
    }
    return node;
}

static Node* parse_member_chain(const char *base_id) {
    return parse_member_chain_from(new_node(NODE_IDENTIFIER, strdup(base_id)), svar_struct(base_id));
}

// n の直後に ++ / -- が続くなら NODE_POSTINC/POSTDEC で包む（value=NULL, left=n）。
// a[i]++ / p->m++ / v.m++ 用（単純識別子の x++ は parse_primary 側で別途処理済み）。
// 配列初期化子  = { e0, e1, ... }  （cur_token が '{' の状態で呼ぶ）
// 要素を next 連鎖にした NODE_INITLIST を返し、要素数を *pcount へ。
static Node* parse_init_list(int *pcount) {
    next_token(); // consume {
    Node *head = NULL, *tail = NULL;
    int cnt = 0;
    while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
        Node *e = parse_expr();
        if (e && e->type == NODE_NEG && e->left && e->left->type == NODE_NUMBER) {
            char b[64];
            snprintf(b, sizeof b, "-%s", e->left->value);
            e = new_node(NODE_NUMBER, strdup(b));
        }
        if (e) { if (!head) head = tail = e; else { tail->next = e; tail = e; } cnt++; }
        if (cur_token.type == TOKEN_COMMA) next_token();
        else break;
    }
    if (cur_token.type == TOKEN_RBRACE) next_token();
    Node *n = new_node(NODE_INITLIST, NULL);
    n->left = head;
    if (pcount) *pcount = cnt;
    return n;
}

static Node* wrap_postfix(Node *n) {
    while (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
        NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_POSTINC : NODE_POSTDEC;
        next_token();
        Node *p = new_node(nt, NULL);
        p->left = n;
        n = p;
    }
    return n;
}

// cur_token が型の始まりか（キャスト / 宣言の判定用）
static int is_type_start() {
    if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG || cur_token.type == TOKEN_CHAR
        || cur_token.type == TOKEN_VOID || cur_token.type == TOKEN_SIGN
        || cur_token.type == TOKEN_STRUCT) return 1;
    if (cur_token.type == TOKEN_IDENTIFIER && struct_by_name(cur_token.value) >= 0) return 1;
    if (cur_token.type == TOKEN_IDENTIFIER && is_tname(cur_token.value)) return 1;
    return 0;
}

// 一次式（数値・文字列・識別子・関数呼び出し・括弧・キャスト）
static Node* parse_primary() {
    if (cur_token.type == TOKEN_LPAREN) {
        next_token(); // consume (
        if (is_type_start()) {
            // キャスト  (type)expr
            int saw_char = 0, saw_wide = 0, saw_long = 0, is_ptr = 0;
            if (cur_token.type == TOKEN_STRUCT) {
                next_token();
                if (cur_token.type == TOKEN_IDENTIFIER) next_token(); // タグ
                saw_wide = 1;
            } else {
                while (cur_token.type == TOKEN_CHAR || cur_token.type == TOKEN_INT
                    || cur_token.type == TOKEN_LONG
                    || cur_token.type == TOKEN_VOID || cur_token.type == TOKEN_SIGN) {
                    if (cur_token.type == TOKEN_CHAR) saw_char = 1;
                    if (cur_token.type == TOKEN_INT)  saw_wide = 1;
                    if (cur_token.type == TOKEN_LONG) saw_long = 1;
                    next_token();
                }
            }
            while (cur_token.type == TOKEN_STAR) { is_ptr = 1; next_token(); }
            if (cur_token.type == TOKEN_RPAREN) next_token();
            Node *inner = parse_unary();
            if (is_ptr) {
                // ポインタキャスト: 値は素通し。long* かどうかだけ記録する
                // （*(long*)addr の 4 バイト読みで参照）。
                Node *c = new_node(NODE_CVT, NULL);
                c->left = inner;
                c->base_type = saw_long ? 2 : 1;
                c->array_size = 1;   // ポインタ
                return c;
            }
            if (saw_char && !saw_wide && !saw_long) {
                // char へのキャスト = 下位1バイト
                Node *m = new_node(NODE_BITAND, NULL);
                m->left = inner;
                m->right = new_node(NODE_NUMBER, strdup("255"));
                return m;
            }
            if (saw_long) {
                Node *c = new_node(NODE_CVT, NULL);   // 32bit へ拡大
                c->left = inner;
                c->base_type = 2;
                return c;
            }
            if (saw_wide) {
                Node *c = new_node(NODE_CVT, NULL);   // 16bit へ縮小（long でなければ実質 no-op）
                c->left = inner;
                c->base_type = 1;
                return c;
            }
            return inner;   // (void) 等
        }
        Node *n = parse_expr();
        if (cur_token.type == TOKEN_RPAREN) next_token();
        return n;
    }
    if (cur_token.type == TOKEN_NUMBER) {
        Node *n = new_node(NODE_NUMBER, cur_token.value);
        n->is_long = cur_token.is_long;
        next_token();
        return n;
    } else if (cur_token.type == TOKEN_STRING) {
        Node *n = new_node(NODE_STRING, cur_token.value);
        next_token();
        return n;
    } else if (cur_token.type == TOKEN_IDENTIFIER) {
        char id_buf[128];
        strcpy(id_buf, cur_token.value);
        next_token();
        if (cur_token.type == TOKEN_NUMBER) {
            strcat(id_buf, cur_token.value);
            next_token();
        }
        if (cur_token.type == TOKEN_LPAREN) {
            next_token(); // consume (
            Node *call = new_node(NODE_CALL, strdup(id_buf));
            Node *arg_list = NULL;
            Node *last_arg = NULL;
            while (cur_token.type != TOKEN_RPAREN && cur_token.type != TOKEN_EOF) {
                Node *arg = parse_expr();
                if (arg) {
                    if (!arg_list) {
                        arg_list = arg;
                        last_arg = arg;
                    } else {
                        last_arg->next = arg;
                        last_arg = arg;
                    }
                }
                if (cur_token.type == TOKEN_COMMA) {
                    next_token();
                } else {
                    // ',' でも ')' でもないトークン（壊れた文字列由来の ';' 等）が来た場合、
                    // parse_expr がこれ以上進めず無限ループになるためここで打ち切る。
                    if (cur_token.type != TOKEN_RPAREN) {
                        fprintf(stderr, "%s:%d: error: unexpected token in argument list\n",
                                filename, cur_token.line);
                        has_error = 1;
                    }
                    break;
                }
            }
            if (cur_token.type == TOKEN_RPAREN) next_token();
            call->left = arg_list;
            return call;
        } else if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
            // 後置 x++ / x--
            NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_POSTINC : NODE_POSTDEC;
            next_token();
            return new_node(nt, strdup(id_buf));
        } else if (cur_token.type == TOKEN_LBRACKET) {
            // a[i]  / a[i].m / a[i]->m / a[i][j] …
            next_token();
            Node *idx = parse_expr();
            if (cur_token.type == TOKEN_RBRACKET) next_token();
            Node *n = new_node(NODE_INDEX, NULL);
            n->left = new_node(NODE_IDENTIFIER, strdup(id_buf));
            n->right = idx;
            int sar = svar_struct(id_buf);
            if (sar >= 0) n->array_size = g_structs[sar].size;
            return wrap_postfix(parse_member_chain_from(n, sar));
        } else if (cur_token.type == TOKEN_ARROW || cur_token.type == TOKEN_DOT) {
            return wrap_postfix(parse_member_chain(id_buf));   // p->m / v.m / a->b->c / p->m[i] … の連鎖
        } else {
            return new_node(NODE_IDENTIFIER, strdup(id_buf));
        }
    }
    return NULL;
}

// 単項 ! - * & ++ -- ~ sizeof
static Node* parse_unary() {
    if (cur_token.type == TOKEN_SIZEOF) {
        next_token();
        int had_paren = 0;
        if (cur_token.type == TOKEN_LPAREN) { had_paren = 1; next_token(); }
        int deref = 0;
        while (cur_token.type == TOKEN_STAR || cur_token.type == TOKEN_AMP) {
            if (cur_token.type == TOKEN_STAR) deref = 1;
            next_token();
        }
        int sz = g_intsz;
        if (cur_token.type == TOKEN_LONG) { sz = 4; next_token();
            while (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG || cur_token.type == TOKEN_SIGN) next_token(); }
        else if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_SIGN) { sz = g_intsz; next_token();
            while (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG || cur_token.type == TOKEN_SIGN) { if (cur_token.type == TOKEN_LONG) sz = 4; next_token(); } }
        else if (cur_token.type == TOKEN_CHAR) { sz = 1; next_token(); }
        else if (cur_token.type == TOKEN_STRUCT) {
            next_token();
            if (cur_token.type == TOKEN_IDENTIFIER) {
                int si = struct_by_name(cur_token.value);
                if (si >= 0) sz = g_structs[si].size;
                next_token();
            }
        } else if (cur_token.type == TOKEN_IDENTIFIER) {
            int si = struct_by_name(cur_token.value);       // 型名?
            if (si >= 0) sz = g_structs[si].size;
            else {
                int vs = svar_struct(cur_token.value);      // struct 変数?
                if (vs >= 0) sz = g_structs[vs].size;       // *p も p も struct サイズ扱い（サブセット）
                else sz = g_intsz;
            }
            next_token();
        }
        (void)deref;
        while (cur_token.type == TOKEN_STAR) { sz = g_intsz; next_token(); }
        if (had_paren && cur_token.type == TOKEN_RPAREN) next_token();
        char nb[16]; sprintf(nb, "%d", sz);
        return new_node(NODE_NUMBER, strdup(nb));
    }
    if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
        // 前置 ++x / --x （x は識別子のみ対応）
        NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_PREINC : NODE_PREDEC;
        next_token();
        if (cur_token.type == TOKEN_IDENTIFIER) {
            Node *n = new_node(nt, strdup(cur_token.value));
            next_token();
            return n;
        }
        return parse_unary();
    }
    if (cur_token.type == TOKEN_NOT) {
        next_token();
        Node *n = new_node(NODE_NOT, NULL);
        n->left = parse_unary();
        return n;
    }
    if (cur_token.type == TOKEN_TILDE) {
        next_token();
        Node *n = new_node(NODE_BITNOT, NULL);
        n->left = parse_unary();
        return n;
    }
    if (cur_token.type == TOKEN_MINUS) {
        next_token();
        Node *n = new_node(NODE_NEG, NULL);
        n->left = parse_unary();
        return n;
    }
    if (cur_token.type == TOKEN_STAR) {
        next_token();
        Node *n = new_node(NODE_DEREF, NULL);   // *p の読み出し
        n->left = parse_unary();
        return n;
    }
    if (cur_token.type == TOKEN_AMP) {
        next_token();
        Node *n = new_node(NODE_ADDR, NULL);    // &x
        n->left = parse_unary();
        return n;
    }
    return parse_primary();
}

// 乗除 * /
static Node* parse_mul() {
    Node *node = parse_unary();
    while (cur_token.type == TOKEN_STAR || cur_token.type == TOKEN_SLASH) {
        TokenType op = cur_token.type;
        next_token();
        Node *right = parse_unary();
        Node *n = new_node(op == TOKEN_STAR ? NODE_MUL : NODE_DIV, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    return node;
}

// 加減 + -
static Node* parse_add() {
    Node *node = parse_mul();
    while (cur_token.type == TOKEN_PLUS || cur_token.type == TOKEN_MINUS) {
        TokenType op = cur_token.type;
        next_token();
        Node *right = parse_mul();
        Node *n = new_node(op == TOKEN_PLUS ? NODE_ADD : NODE_SUB, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    return node;
}

// シフト << >>
static Node* parse_shift() {
    Node *node = parse_add();
    while (cur_token.type == TOKEN_SHL || cur_token.type == TOKEN_SHR) {
        TokenType op = cur_token.type;
        next_token();
        Node *right = parse_add();
        Node *n = new_node(op == TOKEN_SHL ? NODE_SHL : NODE_SHR, NULL);
        n->left = node; n->right = right;
        node = n;
    }
    return node;
}

// 関係比較 < > <= >=
static Node* parse_relational() {
    Node *node = parse_shift();
    while (cur_token.type == TOKEN_LESS || cur_token.type == TOKEN_GREATER ||
           cur_token.type == TOKEN_LE || cur_token.type == TOKEN_GE) {
        TokenType op = cur_token.type;
        next_token();
        Node *right = parse_add();
        NodeType nt = NODE_LT;
        if (op == TOKEN_GREATER) nt = NODE_GT;
        else if (op == TOKEN_LE) nt = NODE_LE;
        else if (op == TOKEN_GE) nt = NODE_GE;
        Node *n = new_node(nt, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    return node;
}

// 等価 == !=
static Node* parse_equality() {
    Node *node = parse_relational();
    while (cur_token.type == TOKEN_EQ || cur_token.type == TOKEN_NE) {
        TokenType op = cur_token.type;
        next_token();
        Node *right = parse_relational();
        Node *n = new_node(op == TOKEN_EQ ? NODE_EQ : NODE_NE, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    return node;
}

// ビット AND &
static Node* parse_bitand() {
    Node *node = parse_equality();
    while (cur_token.type == TOKEN_AMP) {
        next_token();
        Node *right = parse_equality();
        Node *n = new_node(NODE_BITAND, NULL);
        n->left = node; n->right = right;
        node = n;
    }
    return node;
}

// ビット XOR ^
static Node* parse_bitxor() {
    Node *node = parse_bitand();
    while (cur_token.type == TOKEN_CARET) {
        next_token();
        Node *right = parse_bitand();
        Node *n = new_node(NODE_BITXOR, NULL);
        n->left = node; n->right = right;
        node = n;
    }
    return node;
}

// ビット OR |
static Node* parse_bitor() {
    Node *node = parse_bitxor();
    while (cur_token.type == TOKEN_PIPE) {
        next_token();
        Node *right = parse_bitxor();
        Node *n = new_node(NODE_BITOR, NULL);
        n->left = node; n->right = right;
        node = n;
    }
    return node;
}

// 論理積 &&
static Node* parse_and() {
    Node *node = parse_bitor();
    while (cur_token.type == TOKEN_AND) {
        next_token();
        Node *right = parse_bitor();
        Node *n = new_node(NODE_AND, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    return node;
}

// 論理和 || + 三項 ?:（最低優先）
static Node* parse_expr() {
    Node *node = parse_and();
    while (cur_token.type == TOKEN_OR) {
        next_token();
        Node *right = parse_and();
        Node *n = new_node(NODE_OR, NULL);
        n->left = node;
        n->right = right;
        node = n;
    }
    if (cur_token.type == TOKEN_QUESTION) {
        next_token();
        Node *t = parse_expr();
        if (cur_token.type == TOKEN_COLON) next_token();
        Node *f = parse_expr();
        Node *n = new_node(NODE_TERNARY, NULL);
        n->left = node;   // 条件
        n->right = t;
        n->third = f;
        node = n;
    }
    return node;
}

/* 条件式の中の代入 `while ((c = getchar()) != EOF)` を検出して落とす。
 * parse_expr は代入を式として扱えない(代入は parse_simple_stmt_nosemi の
 * 文レベルだけ)。そのため条件は裸の `c` になり、代入はループの外へ追い出され、
 * 比較は捨てられる ── エラーにならず黙って誤コードが出る。
 * tizix の du / tee が実際にこれで壊れていた(#29)。式としての代入を実装する
 * までは受理せず、for(;;) + 明示 break へ書き換えさせる。 */
static void reject_assign_in_cond(const char *ctx) {
    if (cur_token.type != TOKEN_ASSIGN) return;
    fprintf(stderr, "%s:%d: error: assignment inside a %s condition is not supported; "
                    "rewrite as `for (;;) { x = f(); if (...) break; }`\n",
            filename, cur_token.line, ctx);
    has_error = 1;
}

// 識別子で始まる代入 / 関数呼び出し / ++ -- += -=（末尾 ; は消費しない）。for の init/post 用。
static Node* parse_simple_stmt_nosemi() {
    if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
        NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_PREINC : NODE_PREDEC;
        next_token();
        if (cur_token.type == TOKEN_IDENTIFIER) {
            Node *n = new_node(nt, strdup(cur_token.value));
            next_token();
            return n;
        }
        return NULL;
    }
    if (cur_token.type != TOKEN_IDENTIFIER) return NULL;
    char id_buf[128];
    strcpy(id_buf, cur_token.value);
    next_token();
    if (cur_token.type == TOKEN_NUMBER) {
        strcat(id_buf, cur_token.value);
        next_token();
    }
    if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
        NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_POSTINC : NODE_POSTDEC;
        next_token();
        return new_node(nt, strdup(id_buf));
    }
    if (cur_token.type == TOKEN_PLUS_ASSIGN || cur_token.type == TOKEN_MINUS_ASSIGN
        || cur_token.type == TOKEN_STAR_ASSIGN || cur_token.type == TOKEN_SLASH_ASSIGN) {
        NodeType op = (cur_token.type == TOKEN_PLUS_ASSIGN)  ? NODE_ADD :
                      (cur_token.type == TOKEN_MINUS_ASSIGN) ? NODE_SUB :
                      (cur_token.type == TOKEN_STAR_ASSIGN)  ? NODE_MUL : NODE_DIV;
        next_token();
        Node *node = new_node(NODE_ASSIGN, strdup(id_buf));
        Node *bin = new_node(op, NULL);
        bin->left = new_node(NODE_IDENTIFIER, strdup(id_buf));
        bin->right = parse_expr();
        add_child(node, bin);
        return node;
    }
    if (cur_token.type == TOKEN_ASSIGN) {
        next_token(); // consume =
        Node *node = new_node(NODE_ASSIGN, strdup(id_buf));
        Node *right = parse_expr();
        if (right) add_child(node, right);
        return node;
    } else if (cur_token.type == TOKEN_LPAREN) {
        next_token(); // consume (
        Node *call = new_node(NODE_CALL, strdup(id_buf));
        Node *arg_list = NULL;
        Node *last_arg = NULL;
        while (cur_token.type != TOKEN_RPAREN && cur_token.type != TOKEN_EOF) {
            Node *arg = parse_expr();
            if (arg) {
                if (!arg_list) { arg_list = arg; last_arg = arg; }
                else { last_arg->next = arg; last_arg = arg; }
            }
            if (cur_token.type == TOKEN_COMMA) next_token();
            else break;
        }
        if (cur_token.type == TOKEN_RPAREN) next_token();
        call->left = arg_list;
        return call;
    }
    return NULL;
}

// { ... } ブロック、または単文
static Node* parse_block_or_stmt() {
    if (cur_token.type == TOKEN_LBRACE) {
        next_token(); // consume {
        Node *blk = new_node(NODE_BLOCK, "block");
        while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
            Node *s = parse_stmt();
            if (s) add_child(blk, s);
        }
        if (cur_token.type == TOKEN_RBRACE) next_token();
        return blk;
    }
    return parse_stmt();
}

// 文を解析する
static Node* parse_stmt() {
    if (cur_token.type == TOKEN_RETURN) {
        Node *node = new_node(NODE_RETURN, "return");
        next_token();
        if (cur_token.type != TOKEN_SEMICOLON && cur_token.type != TOKEN_EOF) {
            Node *expr = parse_expr();
            if (expr) add_child(node, expr);
        }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return node;
    } else if (cur_token.type == TOKEN_LBRACE) {
        return parse_block_or_stmt();
    } else if (cur_token.type == TOKEN_BREAK) {
        next_token();
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return new_node(NODE_BREAK, NULL);
    } else if (cur_token.type == TOKEN_CONTINUE) {
        next_token();
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return new_node(NODE_CONTINUE, NULL);
    } else if (cur_token.type == TOKEN_SWITCH) {
        next_token();
        Node *node = new_node(NODE_SWITCH, NULL);
        if (cur_token.type == TOKEN_LPAREN) next_token();
        node->left = parse_expr();
        if (cur_token.type == TOKEN_RPAREN) next_token();
        Node *body = new_node(NODE_BLOCK, "block");
        if (cur_token.type == TOKEN_LBRACE) {
            next_token();
            while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
                if (cur_token.type == TOKEN_CASE) {
                    next_token();
                    Node *c = new_node(NODE_CASE, NULL);
                    if (cur_token.type == TOKEN_NUMBER) { c->value = strdup(cur_token.value); next_token(); }
                    if (cur_token.type == TOKEN_COLON) next_token();
                    add_child(body, c);
                } else if (cur_token.type == TOKEN_DEFAULT) {
                    next_token();
                    if (cur_token.type == TOKEN_COLON) next_token();
                    add_child(body, new_node(NODE_DEFAULT, NULL));
                } else {
                    Node *s = parse_stmt();
                    if (s) add_child(body, s);
                }
            }
            if (cur_token.type == TOKEN_RBRACE) next_token();
        }
        node->right = body;
        return node;
    } else if (cur_token.type == TOKEN_ENUM) {
        parse_enum_decl();
        return NULL;
    } else if (cur_token.type == TOKEN_TYPEDEF) {
        next_token();
        if (cur_token.type == TOKEN_ENUM) parse_enum_decl();
        else if (cur_token.type == TOKEN_STRUCT) parse_struct_decl(1);
        else { char last_id[128] = "";
               while (cur_token.type != TOKEN_SEMICOLON && cur_token.type != TOKEN_EOF) {
                   if (cur_token.type == TOKEN_IDENTIFIER) strcpy(last_id, cur_token.value);
                   next_token(); }
               if (last_id[0]) tname_register(last_id);
               if (cur_token.type == TOKEN_SEMICOLON) next_token(); }
        return NULL;
    } else if (cur_token.type == TOKEN_STRUCT ||
               (cur_token.type == TOKEN_IDENTIFIER && struct_by_name(cur_token.value) >= 0)) {
        int sidx;
        if (cur_token.type == TOKEN_STRUCT) {
            sidx = parse_struct_decl(0);
            if (sidx < 0) return NULL;   // 定義を消費した
        } else {
            sidx = struct_by_name(cur_token.value);
            next_token(); // consume 型名
        }
        int is_ptr = 0;
        while (cur_token.type == TOKEN_STAR) { is_ptr = 1; next_token(); }
        if (cur_token.type == TOKEN_IDENTIFIER) {
            char vb[128]; strcpy(vb, cur_token.value); next_token();
            int acount = 1;   // struct 配列 Rec pool[N];
            int had_bracket = 0;
            while (cur_token.type == TOKEN_LBRACKET) {
                had_bracket = 1;
                next_token();
                if (cur_token.type == TOKEN_NUMBER) { acount *= atoi(cur_token.value); next_token(); }
                if (cur_token.type == TOKEN_RBRACKET) next_token();
            }
            Node *decl;
            if (is_ptr && had_bracket) {
                // Struct *name[N] : ポインタ配列（要素はポインタ幅）。struct 値配列ではない。
                decl = new_node(NODE_ARRAY_DECL, strdup(vb));
                decl->array_size = acount;
                decl->base_type = 1;
                /* svar_register しない: name[i] は struct 値ではなくポインタ */
            } else if (is_ptr) {
                decl = new_node(NODE_PTR_DECL, strdup(vb));
                svar_register(vb, sidx);
            } else {
                decl = new_node(NODE_ARRAY_DECL, strdup(vb));
                if (sidx >= 0) decl->array_size = g_structs[sidx].size * acount;
                svar_register(vb, sidx);
            }
            if (cur_token.type == TOKEN_ASSIGN) {
                next_token();
                Node *init = parse_expr();
                if (init) add_child(decl, init);
            }
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return decl;
        }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return NULL;
    } else if (cur_token.type == TOKEN_SEMICOLON) {
        next_token(); // 空文
        return NULL;
    } else if (cur_token.type == TOKEN_IF) {
        next_token();
        Node *node = new_node(NODE_IF, "if");
        if (cur_token.type == TOKEN_LPAREN) next_token();
        node->left = parse_expr();            // 条件
        reject_assign_in_cond("if");
        if (cur_token.type == TOKEN_RPAREN) next_token();
        node->right = parse_block_or_stmt();  // then節
        if (cur_token.type == TOKEN_ELSE) {
            next_token();
            node->third = parse_block_or_stmt(); // else節
        }
        return node;
    } else if (cur_token.type == TOKEN_WHILE) {
        next_token();
        Node *node = new_node(NODE_WHILE, "while");
        if (cur_token.type == TOKEN_LPAREN) next_token();
        node->left = parse_expr();            // 条件
        reject_assign_in_cond("while");
        if (cur_token.type == TOKEN_RPAREN) next_token();
        node->right = parse_block_or_stmt();  // body
        return node;
    } else if (cur_token.type == TOKEN_FOR) {
        next_token();
        Node *node = new_node(NODE_FOR, "for");
        if (cur_token.type == TOKEN_LPAREN) next_token();
        // init
        if (cur_token.type == TOKEN_SEMICOLON) {
            next_token();
        } else if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_CHAR
                   || cur_token.type == TOKEN_SIGN || is_type_start()) {
            node->left = parse_stmt();        // 宣言（末尾 ; を消費。struct/typedef 型も可）
        } else {
            Node *ini = parse_simple_stmt_nosemi();
            Node *itail = ini;
            while (cur_token.type == TOKEN_COMMA) {   // for (i=0, j=n; ...)
                next_token();
                Node *in = parse_simple_stmt_nosemi();
                if (in) { if (itail) { itail->next = in; itail = in; } else ini = itail = in; }
            }
            node->left = ini;
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
        }
        // cond
        if (cur_token.type != TOKEN_SEMICOLON) { node->right = parse_expr(); reject_assign_in_cond("for"); }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        // post（comma 区切り可: p = p->next, i++）
        if (cur_token.type != TOKEN_RPAREN) {
            Node *post = parse_simple_stmt_nosemi();
            Node *ptail = post;
            while (cur_token.type == TOKEN_COMMA) {
                next_token();
                Node *pn = parse_simple_stmt_nosemi();
                if (pn) { if (ptail) { ptail->next = pn; ptail = pn; } else post = ptail = pn; }
            }
            node->third = post;
        }
        if (cur_token.type == TOKEN_RPAREN) next_token();
        node->fourth = parse_block_or_stmt(); // body
        return node;
    } else if (cur_token.type == TOKEN_CHAR || cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG
               || cur_token.type == TOKEN_SIGN
               || (cur_token.type == TOKEN_IDENTIFIER && is_tname(cur_token.value))) {
        int is_int;
        if (cur_token.type == TOKEN_IDENTIFIER) { is_int = 1; next_token(); }
        else is_int = parse_type_run();  // char/int/long/unsigned… の連なりを消費 (0=char/1=int/2=long)
        // SIGN 連なりの後に typedef/struct 型名 (static Node *x, register uint8_t c 等)
        int decl_sidx = -1;
        if (cur_token.type == TOKEN_IDENTIFIER
            && (struct_by_name(cur_token.value) >= 0 || is_tname(cur_token.value))) {
            decl_sidx = struct_by_name(cur_token.value);
            next_token();
        }
        int is_ptr = 0;
        int nstars = 0;
        int is_array = 0;
        int arr_size = 0;
        while (cur_token.type == TOKEN_STAR) {
            is_ptr = 1; nstars++;
            next_token(); // consume *
        }
        if (cur_token.type == TOKEN_LBRACKET) {
            is_array = 1;
            next_token(); // consume [
            if (cur_token.type == TOKEN_NUMBER) {
                arr_size = atoi(cur_token.value);
                next_token();
            }
            if (cur_token.type == TOKEN_RBRACKET) next_token(); // consume ]
        }
        if (cur_token.type == TOKEN_IDENTIFIER) {
            char var_buf[128];
            strcpy(var_buf, cur_token.value);
            next_token();
            if (cur_token.type == TOKEN_NUMBER) {
                strcat(var_buf, cur_token.value);
                next_token();
            }
            // 後置の配列宣言子:  char name[N];  /  char name[];
            if (cur_token.type == TOKEN_LBRACKET) {
                is_array = 1;
                next_token(); // consume [
                if (cur_token.type == TOKEN_NUMBER) {
                    arr_size = atoi(cur_token.value);
                    next_token();
                }
                if (cur_token.type == TOKEN_RBRACKET) next_token(); // consume ]
            }
            NodeType nt = NODE_VAR_DECL;
            if (is_ptr && is_array) nt = NODE_ARRAY_DECL;   // T *name[N] : ポインタ配列
            else if (is_ptr) nt = NODE_PTR_DECL;
            else if (is_array) nt = NODE_ARRAY_DECL;
            else if (decl_sidx >= 0) nt = NODE_ARRAY_DECL;   // struct 値 = バイト塊

            Node *decl = new_node(nt, strdup(var_buf));
            if (is_array && decl_sidx >= 0 && !is_ptr)
                decl->array_size = g_structs[decl_sidx].size * (arr_size > 0 ? arr_size : 1);
            else if (is_ptr && is_array)
                decl->array_size = (arr_size > 0 ? arr_size : 1);
            else if (is_array) decl->array_size = arr_size;
            else if (decl_sidx >= 0 && !is_ptr) decl->array_size = g_structs[decl_sidx].size;
            if ((is_ptr && is_array) || (is_ptr && nstars >= 2)) decl->base_type = 1;
            else if (is_int && decl_sidx < 0) decl->base_type = is_int;  // 1=int / 2=long
            if (decl_sidx >= 0) svar_register(var_buf, decl_sidx);
            if (cur_token.type == TOKEN_ASSIGN) {
                next_token(); // consume =
                if (cur_token.type == TOKEN_LBRACE && decl->type == NODE_ARRAY_DECL) {
                    int cnt = 0;
                    add_child(decl, parse_init_list(&cnt));
                    if (decl->array_size <= 0) decl->array_size = cnt;
                } else {
                    Node *init = parse_expr();
                    if (init) add_child(decl, init);
                }
            }
            // 追加宣言子:  int a = 1, *b, c[4];
            Node *chain_tail = decl;
            while (cur_token.type == TOKEN_COMMA) {
                next_token();
                int p2 = 0, arr2 = 0, asz2 = 0;
                while (cur_token.type == TOKEN_STAR) { p2 = 1; next_token(); }
                if (cur_token.type != TOKEN_IDENTIFIER) break;
                char vb2[128]; strcpy(vb2, cur_token.value); next_token();
                if (cur_token.type == TOKEN_LBRACKET) {
                    arr2 = 1; next_token();
                    if (cur_token.type == TOKEN_NUMBER) { asz2 = atoi(cur_token.value); next_token(); }
                    if (cur_token.type == TOKEN_RBRACKET) next_token();
                }
                NodeType nt2 = p2 ? NODE_PTR_DECL : (arr2 ? NODE_ARRAY_DECL : NODE_VAR_DECL);
                Node *d2 = new_node(nt2, strdup(vb2));
                if (arr2) d2->array_size = (decl_sidx >= 0 ? g_structs[decl_sidx].size : 1) * (asz2 > 0 ? asz2 : 1);
                else if (decl_sidx >= 0 && !p2) { d2->type = NODE_ARRAY_DECL; d2->array_size = g_structs[decl_sidx].size; }
                if (is_int && decl_sidx < 0 && !p2) d2->base_type = is_int;  // 1=int / 2=long
                if (decl_sidx >= 0) svar_register(vb2, decl_sidx);
                if (cur_token.type == TOKEN_ASSIGN) {
                    next_token();
                    Node *in2 = parse_expr();
                    if (in2) add_child(d2, in2);
                }
                chain_tail->next = d2; chain_tail = d2;
            }
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return decl;
        }
    } else if (cur_token.type == TOKEN_STAR) {
        // *p = expr;   （デリファレンス代入。* ident のみ対応）
        next_token(); // consume *
        if (cur_token.type == TOKEN_IDENTIFIER) {
            char pbuf[128];
            strcpy(pbuf, cur_token.value);
            next_token();
            if (cur_token.type == TOKEN_ASSIGN) {
                next_token(); // consume =
                Node *node = new_node(NODE_STORE_DEREF, NULL);
                node->left = new_node(NODE_IDENTIFIER, strdup(pbuf));
                node->right = parse_expr();
                if (cur_token.type == TOKEN_SEMICOLON) next_token();
                return node;
            }
        }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return NULL;
    } else if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
        // 前置 ++i; / --i;
        NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_PREINC : NODE_PREDEC;
        next_token();
        Node *node = NULL;
        if (cur_token.type == TOKEN_IDENTIFIER) {
            node = new_node(nt, strdup(cur_token.value));
            next_token();
        }
        if (cur_token.type == TOKEN_SEMICOLON) next_token();
        return node;
    } else if (cur_token.type == TOKEN_IDENTIFIER) {
        char id_buf[128];
        strcpy(id_buf, cur_token.value);
        next_token();
        if (cur_token.type == TOKEN_NUMBER) {
            strcat(id_buf, cur_token.value);
            next_token();
        }
        if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
            // i++; / i--;
            NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_POSTINC : NODE_POSTDEC;
            next_token();
            Node *node = new_node(nt, strdup(id_buf));
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return node;
        }
        if (cur_token.type == TOKEN_PLUS_ASSIGN || cur_token.type == TOKEN_MINUS_ASSIGN
            || cur_token.type == TOKEN_STAR_ASSIGN || cur_token.type == TOKEN_SLASH_ASSIGN) {
            // i += expr; / i -= expr; / i *= expr; / i /= expr;  →  i = i OP expr;
            NodeType op = (cur_token.type == TOKEN_PLUS_ASSIGN)  ? NODE_ADD :
                          (cur_token.type == TOKEN_MINUS_ASSIGN) ? NODE_SUB :
                          (cur_token.type == TOKEN_STAR_ASSIGN)  ? NODE_MUL : NODE_DIV;
            next_token();
            Node *node = new_node(NODE_ASSIGN, strdup(id_buf));
            Node *bin = new_node(op, NULL);
            bin->left = new_node(NODE_IDENTIFIER, strdup(id_buf));
            bin->right = parse_expr();
            add_child(node, bin);
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return node;
        }
        if (cur_token.type == TOKEN_LBRACKET || cur_token.type == TOKEN_DOT
            || cur_token.type == TOKEN_ARROW) {
            // a[i] / a.m / p->m / a[i].m / p->m[j] … の連鎖 lvalue（末尾に = expr かも）
            Node *first;
            int sidx = svar_struct(id_buf);
            if (cur_token.type == TOKEN_LBRACKET) {
                next_token();
                Node *idx = parse_expr();
                if (cur_token.type == TOKEN_RBRACKET) next_token();
                Node *ix = new_node(NODE_INDEX, NULL);
                ix->left = new_node(NODE_IDENTIFIER, strdup(id_buf));
                ix->right = idx;
                if (sidx >= 0) ix->array_size = g_structs[sidx].size;
                first = parse_member_chain_from(ix, sidx);
            } else {
                first = parse_member_chain_from(new_node(NODE_IDENTIFIER, strdup(id_buf)), sidx);
            }
            if (cur_token.type == TOKEN_INC || cur_token.type == TOKEN_DEC) {
                // a[i]++; / p->m++; / v.m++; （文としての後置 ++/--、評価結果は捨てる）
                NodeType nt = (cur_token.type == TOKEN_INC) ? NODE_POSTINC : NODE_POSTDEC;
                next_token();
                Node *node = new_node(nt, NULL);
                node->left = first;
                if (cur_token.type == TOKEN_SEMICOLON) next_token();
                return node;
            }
            if (cur_token.type == TOKEN_ASSIGN) {
                next_token();
                Node *val = parse_expr();
                if (first->type == NODE_MEMBER) {
                    first->type = NODE_STORE_MEMBER;
                    first->right = val;
                } else if (first->type == NODE_INDEX) {
                    first->type = NODE_STORE_INDEX;
                    first->third = val;
                } else {
                    first = NULL;
                }
                if (cur_token.type == TOKEN_SEMICOLON) next_token();
                return first;
            }
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return NULL;
        }
        if (cur_token.type == TOKEN_ASSIGN) {
            next_token(); // consume =
            Node *node = new_node(NODE_ASSIGN, strdup(id_buf));
            Node *right = parse_expr();
            if (right) add_child(node, right);
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            return node;
        } else if (cur_token.type == TOKEN_LPAREN) {
            next_token(); // consume (
            Node *call = new_node(NODE_CALL, strdup(id_buf));
            Node *arg_list = NULL;
            Node *last_arg = NULL;
            while (cur_token.type != TOKEN_RPAREN && cur_token.type != TOKEN_EOF) {
                Node *arg = parse_expr();
                if (arg) {
                    if (!arg_list) {
                        arg_list = arg;
                        last_arg = arg;
                    } else {
                        last_arg->next = arg;
                        last_arg = arg;
                    }
                }
                if (cur_token.type == TOKEN_COMMA) {
                    next_token();
                } else {
                    // ',' でも ')' でもないトークン（壊れた文字列由来の ';' 等）が来た場合、
                    // parse_expr がこれ以上進めず無限ループになるためここで打ち切る。
                    if (cur_token.type != TOKEN_RPAREN) {
                        fprintf(stderr, "%s:%d: error: unexpected token in argument list\n",
                                filename, cur_token.line);
                        has_error = 1;
                    }
                    break;
                }
            }
            if (cur_token.type == TOKEN_RPAREN) next_token();
            if (cur_token.type == TOKEN_SEMICOLON) next_token();
            call->left = arg_list;
            return call;
        }
    }
    // 未知または消費されなかったトークンを確実に1つ進める
    next_token();
    return NULL;
}

// 簡易的な構文解析（パーサー）
Node* parse() {
    Node *root = new_node(NODE_ROOT, NULL);
    next_token();

    while (cur_token.type != TOKEN_EOF) {
        // #include は lexer 側で展開済み。# が来たら念のため読み飛ばす。
        if (cur_token.type == TOKEN_HASH) {
            next_token();
        } else if (cur_token.type == TOKEN_ENUM) {
            parse_enum_decl();
        } else if (cur_token.type == TOKEN_TYPEDEF) {
            next_token();
            if (cur_token.type == TOKEN_ENUM) parse_enum_decl();
            else if (cur_token.type == TOKEN_STRUCT) parse_struct_decl(1);
            else { char last_id[128] = "";
                   while (cur_token.type != TOKEN_SEMICOLON && cur_token.type != TOKEN_EOF) {
                       if (cur_token.type == TOKEN_IDENTIFIER) strcpy(last_id, cur_token.value);
                       next_token(); }
                   if (last_id[0]) tname_register(last_id);
                   if (cur_token.type == TOKEN_SEMICOLON) next_token(); }
        } else if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG || cur_token.type == TOKEN_CHAR
                   || cur_token.type == TOKEN_VOID || cur_token.type == TOKEN_SIGN
                   || cur_token.type == TOKEN_STRUCT
                   || (cur_token.type == TOKEN_IDENTIFIER && struct_by_name(cur_token.value) >= 0)
                   || (cur_token.type == TOKEN_IDENTIFIER && is_tname(cur_token.value))) {
            int decl_sidx = -1;
            int is_int = 1;
            if (cur_token.type == TOKEN_STRUCT) {
                decl_sidx = parse_struct_decl(0);
            } else if (cur_token.type == TOKEN_IDENTIFIER) {
                decl_sidx = struct_by_name(cur_token.value);   // -1 なら typedef 単純別名
                next_token();
            } else {
                is_int = parse_type_run();
            }
            /* SIGN 連なりの後に型名 (static Node *x, static struct {..} a[N] 等) */
            if (decl_sidx < 0 && cur_token.type == TOKEN_STRUCT) {
                decl_sidx = parse_struct_decl(0);
            }
            if (decl_sidx < 0 && cur_token.type == TOKEN_IDENTIFIER
                && (struct_by_name(cur_token.value) >= 0 || is_tname(cur_token.value))) {
                decl_sidx = struct_by_name(cur_token.value);
                next_token();
            }
            /* 宣言子が続かない (bare `struct Foo {..};` 定義) なら次へ */
            if (decl_sidx >= 0 && cur_token.type == TOKEN_SEMICOLON) { next_token(); continue; }
            int is_ptr = 0;
            int nstars = 0;
            int is_array = 0;
            int arr_size = 0;
            while (cur_token.type == TOKEN_STAR) {
                is_ptr = 1; nstars++;
                next_token(); // consume *
            }
            if (cur_token.type == TOKEN_LBRACKET) {
                is_array = 1;
                next_token(); // consume [
                if (cur_token.type == TOKEN_NUMBER) {
                    arr_size = atoi(cur_token.value);
                    next_token();
                }
                if (cur_token.type == TOKEN_RBRACKET) next_token(); // consume ]
            }
            if (cur_token.type == TOKEN_MAIN || cur_token.type == TOKEN_IDENTIFIER) {
                char var_buf[128];
                strcpy(var_buf, (cur_token.type == TOKEN_MAIN) ? "main" : cur_token.value);
                next_token(); // consume name
                if (cur_token.type == TOKEN_NUMBER) {
                    strcat(var_buf, cur_token.value);
                    next_token();
                }
                // 後置の配列宣言子:  char name[N];  /  char name[];
                if (cur_token.type == TOKEN_LBRACKET) {
                    is_array = 1;
                    next_token(); // consume [
                    if (cur_token.type == TOKEN_NUMBER) {
                        arr_size = atoi(cur_token.value);
                        next_token();
                    }
                    if (cur_token.type == TOKEN_RBRACKET) next_token(); // consume ]
                }

                if (cur_token.type == TOKEN_LPAREN) {
                    Node *func = new_node(NODE_FUNC, strdup(var_buf));
                    func->is_long = (is_int == 2);   // 戻り値が long か（呼び出し側の 32bit 受け取り判定）
                    if (is_int == 2) longfunc_register(var_buf);  // 定義もプロトタイプもここを通る
                    int svar_save = g_nsvars;   // 仮引数含めこの関数のローカル struct-var を後で破棄
                    next_token(); // consume (
                    Node *params = NULL;
                    Node *last_param = NULL;
                    while (cur_token.type != TOKEN_RPAREN && cur_token.type != TOKEN_EOF) {
                        if (cur_token.type == TOKEN_INT || cur_token.type == TOKEN_LONG || cur_token.type == TOKEN_CHAR
                            || cur_token.type == TOKEN_VOID || cur_token.type == TOKEN_SIGN
                            || (cur_token.type == TOKEN_IDENTIFIER && struct_by_name(cur_token.value) >= 0)
                            || (cur_token.type == TOKEN_IDENTIFIER && is_tname(cur_token.value))) {
                            int p_int;
                            int p_struct = -1;
                            if (cur_token.type == TOKEN_IDENTIFIER) {
                                p_struct = struct_by_name(cur_token.value);   // -1 なら typedef 単純別名
                                p_int = 1;   // struct* は 2byte ポインタ
                                next_token();
                            } else {
                                p_int = parse_type_run();
                            }
                            /* SIGN 連なりの後の型名 (const Foo *p) */
                            if (p_struct < 0 && cur_token.type == TOKEN_IDENTIFIER
                                && (struct_by_name(cur_token.value) >= 0 || is_tname(cur_token.value))) {
                                p_struct = struct_by_name(cur_token.value);
                                next_token();
                            }
                            int p_ptr = 0, p_stars = 0;
                            while (cur_token.type == TOKEN_STAR) {
                                p_ptr = 1; p_stars++;
                                next_token(); // consume *
                            }
                            if (cur_token.type == TOKEN_IDENTIFIER) {
                                char p_buf[128];
                                strcpy(p_buf, cur_token.value);
                                next_token();
                                if (cur_token.type == TOKEN_NUMBER) {
                                    strcat(p_buf, cur_token.value);
                                    next_token();
                                }
                                // 後置 [] : char *argv[] 等（ポインタの配列引数 → ポインタ渡し）
                                int p_arr = 0;
                                while (cur_token.type == TOKEN_LBRACKET) {
                                    p_arr = 1; next_token();
                                    if (cur_token.type == TOKEN_NUMBER) next_token();
                                    if (cur_token.type == TOKEN_RBRACKET) next_token();
                                }
                                Node *p_node = new_node(p_ptr ? NODE_PTR_DECL : NODE_VAR_DECL, strdup(p_buf));
                                if (p_int && !p_ptr) p_node->base_type = p_int;  // 1=int / 2=long
                                // char **x / char *x[] : 要素はポインタ幅（x86=8, z80=2）
                                if (p_ptr && (p_stars >= 2 || p_arr)) p_node->base_type = 1;
                                if (p_struct >= 0) svar_register(p_buf, p_struct);
                                if (!params) {
                                    params = p_node;
                                    last_param = p_node;
                                } else {
                                    last_param->next = p_node;
                                    last_param = p_node;
                                }
                            }
                        } else {
                            next_token();
                        }
                        if (cur_token.type == TOKEN_COMMA) next_token();
                    }
                    if (cur_token.type == TOKEN_RPAREN) next_token(); // consume )
                    if (params) func->left = params;

                    if (cur_token.type == TOKEN_LBRACE) {
                        next_token(); // consume {
                        Node *body = new_node(NODE_BLOCK, "block");
                        while (cur_token.type != TOKEN_RBRACE && cur_token.type != TOKEN_EOF) {
                            Node *stmt = parse_stmt();
                            if (stmt) add_child(body, stmt);
                        }
                        /* g_svars（変数→struct 型）の関数ローカル/仮引数分を破棄。
                           スコープレスのままだと別関数の `Node *a` 等が main の
                           `char *a` の添字 stride を汚染する。 */
                        g_nsvars = svar_save;
                        if (func->left) {
                            func->right = body;
                        } else {
                            func->left = body;
                        }
                        if (cur_token.type == TOKEN_RBRACE) next_token(); // consume }
                        add_child(root, func); // 関数定義のみ登録
                    } else {
                        // プロトタイプ宣言 (int foo(int a);) はシンボルを生成せず読み捨てる
                        g_nsvars = svar_save;
                        if (cur_token.type == TOKEN_SEMICOLON) next_token();
                    }
                } else {
                    NodeType nt = NODE_VAR_DECL;
                    if (is_ptr && is_array) nt = NODE_ARRAY_DECL;   // T *name[N] : ポインタ配列
                    else if (is_ptr) nt = NODE_PTR_DECL;
                    else if (is_array) nt = NODE_ARRAY_DECL;
                    else if (decl_sidx >= 0) nt = NODE_ARRAY_DECL;  // struct 値 = バイト塊

                    Node *decl = new_node(nt, strdup(var_buf));
                    if (is_array && decl_sidx >= 0 && !is_ptr)
                        decl->array_size = g_structs[decl_sidx].size * (arr_size > 0 ? arr_size : 1);
                    else if (is_ptr && is_array)
                        decl->array_size = (arr_size > 0 ? arr_size : 1);  // 要素数（emit_data が *8）
                    else if (is_array) decl->array_size = arr_size;
                    else if (decl_sidx >= 0 && !is_ptr)
                        decl->array_size = g_structs[decl_sidx].size;
                    if ((is_ptr && is_array) || (is_ptr && nstars >= 2)) decl->base_type = 1;
                    else if (is_int && decl_sidx < 0) decl->base_type = is_int;  /* 1=int / 2=long。struct 塊は byte 単位で確保済み */
                    /* ポインタ配列 T *name[N] は struct 値配列ではないので svar 登録しない
                       （登録すると name[i] の stride が sizeof(struct) にベイクされる） */
                    if (decl_sidx >= 0 && !(is_ptr && is_array)) svar_register(var_buf, decl_sidx);
                    if (cur_token.type == TOKEN_ASSIGN) {
                        next_token(); // consume =
                        if (cur_token.type == TOKEN_LBRACE && decl->type == NODE_ARRAY_DECL) {
                            int cnt = 0;
                            add_child(decl, parse_init_list(&cnt));
                            if (decl->array_size <= 0) decl->array_size = cnt;
                        } else {
                            Node *init = parse_expr();
                            if (init) add_child(decl, init);
                        }
                    }
                    add_child(root, decl);
                    // 追加宣言子:  int a, *b, c[4];
                    while (cur_token.type == TOKEN_COMMA) {
                        next_token();
                        int p2 = 0, arr2 = 0, asz2 = 0;
                        while (cur_token.type == TOKEN_STAR) { p2 = 1; next_token(); }
                        if (cur_token.type != TOKEN_IDENTIFIER) break;
                        char vb2[128]; strcpy(vb2, cur_token.value); next_token();
                        if (cur_token.type == TOKEN_LBRACKET) {
                            arr2 = 1; next_token();
                            if (cur_token.type == TOKEN_NUMBER) { asz2 = atoi(cur_token.value); next_token(); }
                            if (cur_token.type == TOKEN_RBRACKET) next_token();
                        }
                        NodeType nt2 = p2 ? NODE_PTR_DECL
                                     : ((arr2 || decl_sidx >= 0) ? NODE_ARRAY_DECL : NODE_VAR_DECL);
                        Node *d2 = new_node(nt2, strdup(vb2));
                        if (arr2 && decl_sidx >= 0)
                            d2->array_size = g_structs[decl_sidx].size * (asz2 > 0 ? asz2 : 1);
                        else if (arr2) d2->array_size = asz2;
                        else if (decl_sidx >= 0 && !p2) d2->array_size = g_structs[decl_sidx].size;
                        if (is_int && decl_sidx < 0 && !p2) d2->base_type = is_int;  // 1=int / 2=long
                        if (decl_sidx >= 0) svar_register(vb2, decl_sidx);
                        if (cur_token.type == TOKEN_ASSIGN) {
                            next_token();
                            Node *in2 = parse_expr();
                            if (in2) add_child(d2, in2);
                        }
                        add_child(root, d2);
                    }
                    if (cur_token.type == TOKEN_SEMICOLON) next_token();
                }
            } else {
                next_token();
            }
        } else {
            next_token();
        }
    }
    return root;
}

int main(int argc, char *argv[]) {
    const char *in_file = NULL;
    const char *opt_o = NULL;   // -o <file> : 出力ファイル名を指定
    int opt_X = 0;              // -X        : アセンブラを標準出力へ出して終了
    int opt_s = 0;              // -s        : アセンブラのみ生成して終了（tzcc の既定動作。受理のみ）
    int opt_tizix = 0;         // --tizix-user / -T : 生成後に IY 相対 PIC へ変換
    int opt_x86 = 0;           // --march=x86 : x86-64 バックエンドで出力

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-o") == 0) {
            if (i + 1 < argc) { opt_o = argv[++i]; }
            else { fprintf(stderr, "error: -o requires an argument\n"); return 1; }
        } else if (a[0] == '-' && a[1] == 'o' && a[2]) {
            opt_o = a + 2;                 // -ofoo.s 形式
        } else if (strcmp(a, "-X") == 0) {
            opt_X = 1;
        } else if (strcmp(a, "-s") == 0) {
            opt_s = 1;
        } else if (strcmp(a, "--tizix-user") == 0 || strcmp(a, "-T") == 0) {
            opt_tizix = 1;
            g_dupcheck_fatal = 1;   /* 関数間の同名ローカルはエラー扱い(#31) */
        } else if (strcmp(a, "--march=x86") == 0 || strcmp(a, "--x86") == 0) {
            opt_x86 = 1;
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "warning: ignoring unknown option '%s'\n", a);
        } else {
            in_file = a;
        }
    }
    (void)opt_s;
    if (!in_file) return 1;

    filename = in_file;
    FILE *f = fopen(filename, "r");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);

    // "..." 形式の #include を入力ファイルと同じディレクトリから探せるようにする
    {
        const char *slash = strrchr(filename, '/');
        if (slash) {
            char dir[512];
            size_t dlen = (size_t)(slash - filename);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, filename, dlen);
            dir[dlen] = '\0';
            lexer_set_source_dir(dir);
        }
    }

    g_intsz = opt_x86 ? 8 : 2;   // 構造体レイアウトのため
    lexer_init(buffer);
    Node *ast = parse();

    if (has_error || lexer_error()) {
        free(buffer);
        return 1;
    }

    // -X: アセンブラを標準出力へ出して終了
    if (opt_X && !opt_tizix) {
        if (opt_x86) generate_x86(ast, stdout);
        else         generate_asm(ast, stdout);
        free(buffer);
        return 0;
    }

    // 出力ファイル名生成: -o 指定があればそれ、なければ「入力ファイル名.cより前 + 拡張子.s」
    char *out_filename = NULL;
    if (opt_o) {
        out_filename = malloc(strlen(opt_o) + 1);
        if (out_filename) strcpy(out_filename, opt_o);
    } else {
        size_t len = strlen(filename);
        if (len >= 2 && strcmp(filename + len - 2, ".c") == 0) {
            out_filename = malloc(len + 1);
            if (out_filename) {
                strncpy(out_filename, filename, len - 2);
                out_filename[len - 2] = '\0';
                strcat(out_filename, ".s");
            }
        } else {
            out_filename = malloc(len + 3);
            if (out_filename) {
                strcpy(out_filename, filename);
                strcat(out_filename, ".s");
            }
        }
    }

    if (!out_filename) {
        free(buffer);
        return 1;
    }

    // AST生成に加え、アセンブリ生成を実行
    FILE *asm_out = fopen(out_filename, "w");
    if (asm_out) {
        if (opt_x86) generate_x86(ast, asm_out);
        else         generate_asm(ast, asm_out);
        fclose(asm_out);
    } else {
        fprintf(stderr, "Error: cannot open output file %s\n", out_filename);
        free(out_filename);
        free(buffer);
        return 1;
    }

    // --tizix-user: 生成した .s を IY 相対 PIC へ変換（in-place）
    if (opt_tizix) {
        if (tizix_iy_transform(out_filename) != 0) {
            fprintf(stderr, "Error: tizix IY transform failed on %s\n", out_filename);
            free(out_filename);
            free(buffer);
            return 1;
        }
        // -X 併用時は変換後の .s を標準出力へも出す
        if (opt_X) {
            FILE *rf = fopen(out_filename, "r");
            if (rf) {
                int c;
                while ((c = fgetc(rf)) != EOF) fputc(c, stdout);
                fclose(rf);
            }
        }
    }

    free(out_filename);
    free(buffer);
    return 0;
}
