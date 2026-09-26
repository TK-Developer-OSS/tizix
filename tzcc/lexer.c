#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"

static char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new = malloc(len);
    if (new) memcpy(new, s, len);
    return new;
}

static const char *src;
static int line = 1;
static char last_error_char = 0;

/* ------------------------------------------------------------------ */
/* #include 対応                                                       */
/* ------------------------------------------------------------------ */
#define MAX_INCLUDE_DEPTH 16
#define MAX_INCLUDED      256

typedef struct {
    const char *src;   /* 戻り先の読み取り位置       */
    int line;          /* 戻り先の行番号             */
    char *buf;         /* 戻り先が使っていたバッファ  */
} IncFrame;

static IncFrame inc_stack[MAX_INCLUDE_DEPTH];
static int inc_sp = 0;
static char *cur_buf = NULL;   /* 今レキシングしているバッファ(元ソースは main.c 所有なので NULL) */

static char *included[MAX_INCLUDED];
static int included_n = 0;

/* ------------------------------------------------------------------ */
/* #define オブジェクト形式マクロ（関数形式は未対応）                     */
/* ------------------------------------------------------------------ */
#define MAX_MACROS 4096
static char *macro_name[MAX_MACROS];
static char *macro_body[MAX_MACROS];
static int macro_n = 0;

static const char *macro_lookup(const char *name) {
    for (int i = macro_n - 1; i >= 0; i--)
        if (strcmp(macro_name[i], name) == 0) return macro_body[i];
    return NULL;
}

void lexer_define_macro(const char *name, const char *body) {
    if (!name || !*name || macro_n >= MAX_MACROS) return;
    macro_name[macro_n] = my_strdup(name);
    macro_body[macro_n] = my_strdup(body ? body : "");
    macro_n++;
}

static char source_dir[512] = "";
static int lexer_had_error = 0;

void lexer_set_source_dir(const char *dir) {
    if (!dir) { source_dir[0] = '\0'; return; }
    strncpy(source_dir, dir, sizeof(source_dir) - 1);
    source_dir[sizeof(source_dir) - 1] = '\0';
}

int lexer_error(void) { return lexer_had_error; }

void lexer_init(const char *source) {
    src = source;
    line = 1;
    inc_sp = 0;
    cur_buf = NULL;
    included_n = 0;
    macro_n = 0;
    lexer_had_error = 0;
    // 標準ヘッダ相当の組み込み定数（システムヘッダ未展開でも使えるように）
    lexer_define_macro("NULL", "0");
    lexer_define_macro("EOF", "-1");
    lexer_define_macro("SEEK_SET", "0");
    lexer_define_macro("SEEK_CUR", "1");
    lexer_define_macro("SEEK_END", "2");
}

char lexer_get_last_error_char() {
    return last_error_char;
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *b = malloc(sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t n = fread(b, 1, (size_t)sz, f);
    b[n] = '\0';
    fclose(f);
    return b;
}

/* fname を検索パスで解決。見つかれば out にパスを詰めて 1 */
static int resolve_include(const char *fname, int is_system, char *out, size_t outsz) {
    const char *dirs[2];
    int nd = 0;
    if (is_system) {
        dirs[nd++] = "include";
        if (source_dir[0]) dirs[nd++] = source_dir;
    } else {
        if (source_dir[0]) dirs[nd++] = source_dir;
        dirs[nd++] = "include";
    }
    for (int i = 0; i < nd; i++) {
        snprintf(out, outsz, "%s/%s", dirs[i], fname);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return 1; }
    }
    snprintf(out, outsz, "%s", fname);
    FILE *f = fopen(out, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* src は "include" の直後を指している状態で呼ぶ */
static void do_include_directive(void) {
    while (*src == ' ' || *src == '\t') src++;
    char close = 0;
    if (*src == '<') { close = '>'; src++; }
    else if (*src == '"') { close = '"'; src++; }
    else {
        while (*src && *src != '\n') src++;   /* 不正な書式は行ごと無視 */
        return;
    }
    char fname[256];
    int i = 0;
    while (*src && *src != close && *src != '\n' && i < (int)sizeof(fname) - 1)
        fname[i++] = *src++;
    fname[i] = '\0';
    if (*src == close) src++;
    while (*src && *src != '\n') src++;       /* 行末まで読み飛ばす */

    int is_system = (close == '>');
    char path[600];
    if (!resolve_include(fname, is_system, path, sizeof(path))) {
        if (is_system) {
            fprintf(stderr, "warning: system header <%s> not found, ignored\n", fname);
        } else {
            fprintf(stderr, "error: cannot open include file \"%s\"\n", fname);
            lexer_had_error = 1;
        }
        return;
    }
    /* 多重 include 防止（暗黙の #pragma once） */
    for (int k = 0; k < included_n; k++)
        if (strcmp(included[k], path) == 0) return;
    if (included_n < MAX_INCLUDED) included[included_n++] = my_strdup(path);

    if (inc_sp >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "error: #include nested too deeply\n");
        lexer_had_error = 1;
        return;
    }
    char *buf = read_whole_file(path);
    if (!buf) {
        fprintf(stderr, "error: cannot read include file \"%s\"\n", path);
        lexer_had_error = 1;
        return;
    }
    inc_stack[inc_sp].src = src;
    inc_stack[inc_sp].line = line;
    inc_stack[inc_sp].buf = cur_buf;
    inc_sp++;
    cur_buf = buf;
    src = buf;
    line = 1;
}

/* 構造体を値返し/値代入するコードを tzcc 自身がまだ出せないため、
   結果は静的バッファに置いてポインタを返す（セルフホスト対応）。*/
static Token _lx_result;
Token *lexer_next_token() {
    Token *tk = &_lx_result;
    tk->value = NULL;
    tk->is_long = 0;

    for (;;) {
        while (*src && isspace((unsigned char)*src)) {
            if (*src == '\n') line++;
            src++;
        }

        if (!*src) {
            if (inc_sp > 0) {
                if (cur_buf) free(cur_buf);
                inc_sp--;
                src     = inc_stack[inc_sp].src;
                line    = inc_stack[inc_sp].line;
                cur_buf = inc_stack[inc_sp].buf;
                continue;
            }
            tk->line = line;
            tk->type = TOKEN_EOF;
            return tk;
        }

        /* 行コメント // ... 行末まで */
        if (src[0] == '/' && src[1] == '/') {
            src += 2;
            while (*src && *src != '\n') src++;
            continue;
        }
        /* ブロックコメント (slash-star ... star-slash) ネストなし */
        if (src[0] == '/' && src[1] == '*') {
            src += 2;
            while (*src && !(src[0] == '*' && src[1] == '/')) {
                if (*src == '\n') line++;
                src++;
            }
            if (*src) src += 2;   /* 閉じ星スラッシュを消費 */
            continue;
        }

        if (*src == '#') {
            src++;
            while (*src == ' ' || *src == '\t') src++;
            char d[16];
            int di = 0;
            while (isalpha((unsigned char)*src) && di < 15) d[di++] = *src++;
            d[di] = '\0';
            if (strcmp(d, "include") == 0) {
                do_include_directive();
            } else if (strcmp(d, "define") == 0) {
                while (*src == ' ' || *src == '\t') src++;
                char nm[128]; int ni = 0;
                while ((isalnum((unsigned char)*src) || *src == '_') && ni < 127) nm[ni++] = *src++;
                nm[ni] = '\0';
                if (*src == '(') {
                    /* 関数形式マクロ #define F(x) ... は未対応 → 行ごと無視 */
                    while (*src && *src != '\n') src++;
                } else {
                    while (*src == ' ' || *src == '\t') src++;
                    char bd[256]; int bi = 0;
                    while (*src && *src != '\n' && bi < 255) {
                        /* 行コメントは本体に含めない */
                        if (src[0] == '/' && (src[1] == '/' || src[1] == '*')) break;
                        bd[bi++] = *src++;
                    }
                    while (bi > 0 && (bd[bi-1] == ' ' || bd[bi-1] == '\t' || bd[bi-1] == '\r')) bi--;
                    bd[bi] = '\0';
                    if (ni > 0 && macro_n < MAX_MACROS) {
                        macro_name[macro_n] = my_strdup(nm);
                        macro_body[macro_n] = my_strdup(bd);
                        macro_n++;
                    }
                    while (*src && *src != '\n') src++;
                }
            } else {
                /* #ifndef / #pragma など未対応ディレクティブは行ごと無視 */
                while (*src && *src != '\n') src++;
            }
            continue;
        }

        break;
    }

    tk->line = line;

    if (*src == '(') { src++; tk->type = TOKEN_LPAREN; return tk; }
    if (*src == ')') { src++; tk->type = TOKEN_RPAREN; return tk; }
    if (*src == '{') { src++; tk->type = TOKEN_LBRACE; return tk; }
    if (*src == '}') { src++; tk->type = TOKEN_RBRACE; return tk; }
    if (*src == '[') { src++; tk->type = TOKEN_LBRACKET; return tk; }
    if (*src == ']') { src++; tk->type = TOKEN_RBRACKET; return tk; }
    if (*src == '+') {
        src++;
        if (*src == '+') { src++; tk->type = TOKEN_INC; return tk; }
        if (*src == '=') { src++; tk->type = TOKEN_PLUS_ASSIGN; return tk; }
        tk->type = TOKEN_PLUS; return tk;
    }
    if (*src == '-') {
        src++;
        if (*src == '-') { src++; tk->type = TOKEN_DEC; return tk; }
        if (*src == '=') { src++; tk->type = TOKEN_MINUS_ASSIGN; return tk; }
        if (*src == '>') { src++; tk->type = TOKEN_ARROW; return tk; }
        tk->type = TOKEN_MINUS; return tk;
    }
    if (*src == '*') {
        src++;
        if (*src == '=') { src++; tk->type = TOKEN_STAR_ASSIGN; return tk; }
        tk->type = TOKEN_STAR; return tk;
    }
    if (*src == '/') {
        src++;
        if (*src == '=') { src++; tk->type = TOKEN_SLASH_ASSIGN; return tk; }
        tk->type = TOKEN_SLASH; return tk;
    }
    if (*src == '&') {
        src++;
        if (*src == '&') { src++; tk->type = TOKEN_AND; return tk; }
        tk->type = TOKEN_AMP; return tk;
    }
    if (*src == '|') {
        src++;
        if (*src == '|') { src++; tk->type = TOKEN_OR; return tk; }
        tk->type = TOKEN_PIPE; return tk;
    }
    if (*src == ',') { src++; tk->type = TOKEN_COMMA; return tk; }
    if (*src == '=') {
        src++;
        if (*src == '=') { src++; tk->type = TOKEN_EQ; return tk; }
        tk->type = TOKEN_ASSIGN; return tk;
    }
    if (*src == '!') {
        src++;
        if (*src == '=') { src++; tk->type = TOKEN_NE; return tk; }
        tk->type = TOKEN_NOT; return tk;
    }
    if (*src == ';') { src++; tk->type = TOKEN_SEMICOLON; return tk; }
    if (*src == '.') { src++; tk->type = TOKEN_DOT; return tk; }
    if (*src == '^') { src++; tk->type = TOKEN_CARET; return tk; }
    if (*src == '~') { src++; tk->type = TOKEN_TILDE; return tk; }
    if (*src == '?') { src++; tk->type = TOKEN_QUESTION; return tk; }
    if (*src == ':') { src++; tk->type = TOKEN_COLON; return tk; }
    if (*src == '<') {
        src++;
        if (*src == '<') { src++; tk->type = TOKEN_SHL; return tk; }
        if (*src == '=') { src++; tk->type = TOKEN_LE; return tk; }
        tk->type = TOKEN_LESS; return tk;
    }
    if (*src == '>') {
        src++;
        if (*src == '>') { src++; tk->type = TOKEN_SHR; return tk; }
        if (*src == '=') { src++; tk->type = TOKEN_GE; return tk; }
        tk->type = TOKEN_GREATER; return tk;
    }

    if (isalpha(*src) || *src == '_') {
        char buffer[128];
        int i = 0;
        while (isalnum(*src) || *src == '_') {
            buffer[i++] = *src++;
        }
        buffer[i] = '\0';

        /* #define マクロ展開: 本体を新しいバッファとして push し、再レキシング */
        {
            const char *mb = macro_lookup(buffer);
            if (mb && inc_sp < MAX_INCLUDE_DEPTH) {
                inc_stack[inc_sp].src = src;
                inc_stack[inc_sp].line = line;
                inc_stack[inc_sp].buf = cur_buf;
                inc_sp++;
                cur_buf = my_strdup(mb);
                src = cur_buf;
                return lexer_next_token();
            }
        }

        if (strcmp(buffer, "int") == 0) tk->type = TOKEN_INT;
        // FILE / size_t 系はサブセットでは int 相当の型キーワードとして扱う
        else if (strcmp(buffer, "FILE") == 0) tk->type = TOKEN_INT;
        else if (strcmp(buffer, "short") == 0) tk->type = TOKEN_INT;   // 16bit (= int)
        else if (strcmp(buffer, "long") == 0
              || strcmp(buffer, "uint32_t") == 0 || strcmp(buffer, "int32_t") == 0) tk->type = TOKEN_LONG; // 32bit
        else if (strcmp(buffer, "uint8_t") == 0 || strcmp(buffer, "int8_t") == 0) tk->type = TOKEN_CHAR;   // 8bit
        else if (strcmp(buffer, "size_t") == 0 || strcmp(buffer, "ssize_t") == 0
              || strcmp(buffer, "uintptr_t") == 0 || strcmp(buffer, "intptr_t") == 0
              || strcmp(buffer, "uint16_t") == 0 || strcmp(buffer, "int16_t") == 0) tk->type = TOKEN_INT;
        else if (strcmp(buffer, "unsigned") == 0 || strcmp(buffer, "signed") == 0
              || strcmp(buffer, "const") == 0 || strcmp(buffer, "volatile") == 0
              || strcmp(buffer, "register") == 0 || strcmp(buffer, "static") == 0
              || strcmp(buffer, "auto") == 0 || strcmp(buffer, "inline") == 0
              || strcmp(buffer, "restrict") == 0 || strcmp(buffer, "extern") == 0) tk->type = TOKEN_SIGN;
        else if (strcmp(buffer, "char") == 0 || strcmp(buffer, "chat") == 0) tk->type = TOKEN_CHAR;
        else if (strcmp(buffer, "void") == 0) tk->type = TOKEN_VOID;
        else if (strcmp(buffer, "main") == 0) tk->type = TOKEN_MAIN;
        else if (strcmp(buffer, "return") == 0) tk->type = TOKEN_RETURN;
        else if (strcmp(buffer, "if") == 0) tk->type = TOKEN_IF;
        else if (strcmp(buffer, "else") == 0) tk->type = TOKEN_ELSE;
        else if (strcmp(buffer, "while") == 0) tk->type = TOKEN_WHILE;
        else if (strcmp(buffer, "for") == 0) tk->type = TOKEN_FOR;
        else if (strcmp(buffer, "break") == 0) tk->type = TOKEN_BREAK;
        else if (strcmp(buffer, "continue") == 0) tk->type = TOKEN_CONTINUE;
        else if (strcmp(buffer, "switch") == 0) tk->type = TOKEN_SWITCH;
        else if (strcmp(buffer, "case") == 0) tk->type = TOKEN_CASE;
        else if (strcmp(buffer, "default") == 0) tk->type = TOKEN_DEFAULT;
        else if (strcmp(buffer, "enum") == 0) tk->type = TOKEN_ENUM;
        else if (strcmp(buffer, "struct") == 0) tk->type = TOKEN_STRUCT;
        else if (strcmp(buffer, "union") == 0) tk->type = TOKEN_STRUCT; // union は struct と同扱い（サブセット）
        else if (strcmp(buffer, "typedef") == 0) tk->type = TOKEN_TYPEDEF;
        else if (strcmp(buffer, "sizeof") == 0) tk->type = TOKEN_SIZEOF;
        else if (strcmp(buffer, "include") == 0) tk->type = TOKEN_INCLUDE;
        else {
            tk->type = TOKEN_IDENTIFIER;
            tk->value = my_strdup(buffer);
        }
        return tk;
    }

    if (isdigit(*src)) {
        char buffer[128];
        int i = 0;
        if (src[0] == '0' && (src[1] == 'x' || src[1] == 'X')) {
            // 16 進リテラル 0x.... （sdasz80 も 0x を受ける）
            buffer[i++] = *src++;                 // '0'
            buffer[i++] = *src++;                 // 'x'
            while (isxdigit((unsigned char)*src) && i < 126) buffer[i++] = *src++;
        } else {
            while (isdigit((unsigned char)*src) && i < 126) buffer[i++] = *src++;
        }
        // 整数リテラル接尾辞 u/U/l/L。l/L があれば 32bit(long) 扱いにする。
        while (*src == 'u' || *src == 'U' || *src == 'l' || *src == 'L') {
            if (*src == 'l' || *src == 'L') tk->is_long = 1;
            src++;
        }
        buffer[i] = '\0';
        tk->type = TOKEN_NUMBER;
        tk->value = my_strdup(buffer);
        return tk;
    }

    if (*src == '\'') {
        src++;
        int val = 0;
        if (*src == '\\') {
            src++;
            if (*src == 'n') val = '\n';
            else if (*src == 'r') val = '\r';
            else if (*src == 't') val = '\t';
            else if (*src == '0') val = 0;
            else if (*src) val = *src;
            if (*src) src++;
        } else if (*src) {
            val = (unsigned char)*src++;
        }
        if (*src == '\'') src++;
        char numbuf[32];
        sprintf(numbuf, "%d", val);
        tk->type = TOKEN_NUMBER;
        tk->value = my_strdup(numbuf);
        return tk;
    }

    if (*src == '"') {
        src++;
        char buffer[256];
        int i = 0;
        int unclosed = 0;
        while (*src && *src != '"' && *src != '\n') {
            // 壊れた表記 `a = "H;` （閉じ '"' 無しの1〜2文字 + ';'）だけ「閉じ忘れ」扱い。
            // ";..." のような ';' で始まる正当な文字列や、';' を含む長い文字列は素通し。
            if (*src == ';' && i >= 1 && i <= 2) {
                unclosed = 1;
                break;
            }
            if (*src == '\\') {
                src++;
                if (*src == 'n') buffer[i++] = '\n';
                else if (*src == 'r') buffer[i++] = '\r';
                else if (*src == 't') buffer[i++] = '\t';
                else if (*src == '0') buffer[i++] = '\0';
                else if (*src) buffer[i++] = *src;
                if (*src) src++;
            } else {
                buffer[i++] = *src++;
            }
        }
        buffer[i] = '\0';
        if (!unclosed && *src == '"') src++;

        if (unclosed && i == 1) {
            char numbuf[32];
            sprintf(numbuf, "%d", (unsigned char)buffer[0]);
            tk->type = TOKEN_NUMBER;
            tk->value = my_strdup(numbuf);
            return tk;
        }

        tk->type = TOKEN_STRING;
        tk->value = my_strdup(buffer);
        return tk;
    }

    last_error_char = *src;
    tk->type = TOKEN_UNKNOWN;
    if (*src) src++;
    return tk;
}
