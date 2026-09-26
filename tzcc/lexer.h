#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_EOF,
    TOKEN_INT,
    TOKEN_LONG,   // long / uint32_t / int32_t  → 32bit
    TOKEN_CHAR,
    TOKEN_VOID,
    TOKEN_MAIN,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_ASSIGN,
    TOKEN_DOT,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_RETURN,
    TOKEN_INCLUDE,
    TOKEN_HASH,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_EQ,      // ==
    TOKEN_NE,      // !=
    TOKEN_LE,      // <=
    TOKEN_GE,      // >=
    TOKEN_NOT,     // !
    TOKEN_AMP,     // &  (アドレス取得 / ビットAND)
    TOKEN_AND,     // &&
    TOKEN_PIPE,    // |  (ビットOR)
    TOKEN_OR,      // ||
    TOKEN_CARET,   // ^  (ビットXOR)
    TOKEN_TILDE,   // ~  (ビットNOT)
    TOKEN_SHL,     // <<
    TOKEN_SHR,     // >>
    TOKEN_INC,     // ++
    TOKEN_DEC,     // --
    TOKEN_PLUS_ASSIGN,  // +=
    TOKEN_MINUS_ASSIGN, // -=
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_ENUM,
    TOKEN_STRUCT,
    TOKEN_TYPEDEF,
    TOKEN_SIZEOF,
    TOKEN_ARROW,   // ->
    TOKEN_SIGN,    // unsigned / signed （幅に影響しない型修飾。読み飛ばす）
    TOKEN_QUESTION,// ?
    TOKEN_COLON,   // :
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_STAR_ASSIGN,  // *=
    TOKEN_SLASH_ASSIGN, // /=
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int is_long;  // TOKEN_NUMBER: リテラルに l/L 接尾辞が付いていた（32bit 扱い）
} Token;

void lexer_init(const char *source);
char lexer_get_last_error_char();
Token *lexer_next_token();

/* パーサから定数（enum 値など）をマクロ表へ登録する */
void lexer_define_macro(const char *name, const char *body);

/* #include 用: メインソースが置かれているディレクトリを "..." 検索の基点にする */
void lexer_set_source_dir(const char *dir);
/* #include の解決に失敗したら 1 */
int lexer_error(void);

#endif
