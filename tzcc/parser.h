#ifndef PARSER_H
#define PARSER_H

typedef enum {
    NODE_ROOT,
    NODE_INCLUDE,
    NODE_FUNC,
    NODE_BLOCK,
    NODE_EXPR_STMT,
    NODE_RETURN,
    NODE_CALL,
    NODE_VAR_DECL,
    NODE_ARRAY_DECL,
    NODE_PTR_DECL,
    NODE_ASSIGN,
    NODE_IDENTIFIER,
    NODE_NUMBER,
    NODE_STRING,
    NODE_ARG,
    NODE_ADD,
    NODE_SUB,
    NODE_MUL,
    NODE_DIV,
    NODE_EQ,   // ==
    NODE_NE,   // !=
    NODE_LT,   // <
    NODE_GT,   // >
    NODE_LE,   // <=
    NODE_GE,   // >=
    NODE_NOT,  // 単項 !
    NODE_NEG,  // 単項 -
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_INDEX,        // a[i] の読み出し   left=base, right=index
    NODE_STORE_INDEX,  // a[i] = v          left=base, right=index, third=value
    NODE_DEREF,        // *p の読み出し     left=ptr式
    NODE_STORE_DEREF,  // *p = v            left=ptr式, right=value
    NODE_ADDR,         // &x               left=IDENTIFIER
    NODE_PREINC,       // ++x  value=varname（評価結果=新しい値）。a[i]++/p->m++/v.m++等の一般形は
    NODE_PREDEC,       // --x  value=NULL, left=target（NODE_INDEX/NODE_MEMBER）で表す。
    NODE_POSTINC,      // x++  value=varname （評価結果=古い値）。一般形は value=NULL, left=target。
    NODE_POSTDEC,      // x--
    NODE_AND,          // &&  （短絡）
    NODE_OR,           // ||  （短絡）
    NODE_BITAND,       // &
    NODE_BITOR,        // |
    NODE_BITXOR,       // ^
    NODE_BITNOT,       // ~ （単項）
    NODE_SHL,          // <<
    NODE_SHR,          // >>
    NODE_MEMBER,       // p->m / v.m の読み  left=base識別子, array_size=byteオフセット,
                       //                    base_type: 0=char(1) / 1=int等(2)  value: "->" or "."
    NODE_STORE_MEMBER, // p->m = v / v.m = v  さらに right=値
    NODE_TERNARY,      // c ? t : f    left=c right=t third=f
    NODE_CVT,          // 型変換  left=式  base_type: 1=16bitへ縮小 / 2=32bitへ拡大
                       //         array_size: 1=ポインタキャスト(値は素通し)
    NODE_INITLIST,     // 配列の { a, b, c } 初期化子。left=要素の next 連鎖
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_SWITCH,       // left=式  right=本体(NODE_BLOCK, 中に NODE_CASE/DEFAULT/文)
    NODE_CASE,         // value=数値文字列（default は value=NULL）
    NODE_DEFAULT
} NodeType;

typedef struct Node {
    NodeType type;
    char *value;
    int array_size; // NODE_ARRAY_DECL のときの宣言要素数（0 = 未指定 [] ）
    int base_type;  // 宣言ノードの基底型: 0 = char(1byte), 1 = int(2byte), 2 = long(4byte)
    int is_long;    // NODE_NUMBER: リテラルが long（l/L 接尾辞） / NODE_FUNC: 戻り値が long
    int esz;        // NODE_MEMBER(スカラー): メンバ実バイトサイズ 1/2/4 (0=未設定→2扱い)
    struct Node *left;
    struct Node *right;
    struct Node *third;  // if: else節 / for: post式
    struct Node *fourth; // for: body
    struct Node *next; // リスト用
} Node;

Node* new_node(NodeType type, char *value);
void add_child(Node *parent, Node *child);
void print_ast(Node *node, int depth);

#endif
