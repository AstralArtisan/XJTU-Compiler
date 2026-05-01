#include "scanner.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *word;
    TokenKind kind;
} KEYWORDS[] = {
    {"int", TK_INT},     {"float", TK_FLOAT_KW}, {"void", TK_VOID},
    {"if", TK_IF},       {"else", TK_ELSE},      {"while", TK_WHILE},
    {"return", TK_RETURN},{"input", TK_INPUT},   {"print", TK_PRINT},
};
static const size_t KEYWORD_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

void scanner_init(Scanner *s, const char *src) {
    s->src = src;
    s->pos = 0;
    s->line = 1;
    s->col = 1;
    s->error_count = 0;
}

static int peek(Scanner *s, size_t k) {
    size_t i = s->pos + k;
    unsigned char c = (unsigned char)s->src[i];
    return c ? c : -1;
}

static int advance(Scanner *s) {
    int c = peek(s, 0);
    if (c == -1) return -1;
    s->pos++;
    if (c == '\n') { s->line++; s->col = 1; }
    else { s->col++; }
    return c;
}

static void skip_ws_and_comments(Scanner *s) {
    for (;;) {
        int c = peek(s, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(s);
        } else if (c == '/' && peek(s, 1) == '/') {
            while (peek(s, 0) != -1 && peek(s, 0) != '\n') advance(s);
        } else if (c == '/' && peek(s, 1) == '*') {
            advance(s); advance(s);
            while (peek(s, 0) != -1 && !(peek(s, 0) == '*' && peek(s, 1) == '/'))
                advance(s);
            if (peek(s, 0) != -1) { advance(s); advance(s); }
        } else {
            break;
        }
    }
}
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_digit(int c) { return c >= '0' && c <= '9'; }
static bool is_alnum(int c) { return is_alpha(c) || is_digit(c); }

static void emit_lexeme(Scanner *s, size_t start, Token *out) {
    size_t len = s->pos - start;
    if (len >= TOKEN_LEXEME_LIMIT) len = TOKEN_LEXEME_LIMIT - 1;
    memcpy(out->lexeme, s->src + start, len);
    out->lexeme[len] = '\0';
}

static TokenKind classify_word(const char *word) {
    for (size_t i = 0; i < KEYWORD_COUNT; ++i) {
        if (strcmp(word, KEYWORDS[i].word) == 0) return KEYWORDS[i].kind;
    }
    return TK_ID;
}

static bool scan_number(Scanner *s, Token *out, size_t start, int start_line, int start_col) {
    while (is_digit(peek(s, 0))) advance(s);

    bool is_float = false;
    if (peek(s, 0) == '.') {
        is_float = true;
        advance(s);
        while (is_digit(peek(s, 0))) advance(s);
    }

    int e = peek(s, 0);
    if (e == 'e' || e == 'E') {
        size_t save_pos = s->pos;
        int save_line = s->line, save_col = s->col;
        advance(s);
        if (peek(s, 0) == '+' || peek(s, 0) == '-') advance(s);
        if (!is_digit(peek(s, 0))) {
            s->pos = save_pos;
            s->line = save_line;
            s->col = save_col;
        } else {
            is_float = true;
            while (is_digit(peek(s, 0))) advance(s);
        }
    }

    out->kind = is_float ? TK_FLOAT_LIT : TK_NUM;
    out->line = start_line;
    out->col = start_col;
    emit_lexeme(s, start, out);
    return true;
}

bool scanner_next(Scanner *s, Token *out) {
    skip_ws_and_comments(s);
    int start_line = s->line, start_col = s->col;
    size_t start = s->pos;
    int c = peek(s, 0);

    if (c == -1) {
        out->kind = TK_EOF;
        out->lexeme[0] = '\0';
        out->line = start_line;
        out->col = start_col;
        return false;
    }

    if (is_alpha(c)) {
        while (is_alnum(peek(s, 0))) advance(s);
        emit_lexeme(s, start, out);
        out->kind = classify_word(out->lexeme);
        out->line = start_line;
        out->col = start_col;
        return true;
    }

    if (is_digit(c)) {
        return scan_number(s, out, start, start_line, start_col);
    }

    if (c == '.') {
        if (is_digit(peek(s, 1))) {
            advance(s);
            while (is_digit(peek(s, 0))) advance(s);
            int e2 = peek(s, 0);
            if (e2 == 'e' || e2 == 'E') {
                size_t save_pos = s->pos;
                int save_line = s->line, save_col = s->col;
                advance(s);
                if (peek(s, 0) == '+' || peek(s, 0) == '-') advance(s);
                if (!is_digit(peek(s, 0))) {
                    s->pos = save_pos; s->line = save_line; s->col = save_col;
                } else {
                    while (is_digit(peek(s, 0))) advance(s);
                }
            }
            out->kind = TK_FLOAT_LIT;
            out->line = start_line; out->col = start_col;
            emit_lexeme(s, start, out);
            return true;
        }
        advance(s);
        out->kind = TK_DOT;
        out->line = start_line; out->col = start_col;
        emit_lexeme(s, start, out);
        return true;
    }

    advance(s);
    int n = peek(s, 0);
    TokenKind k = TK_ERR;
    switch (c) {
        case '+':
            if (n == '+') { advance(s); k = TK_AAA; }
            else if (n == '=') { advance(s); k = TK_AAS; }
            else k = TK_ADD;
            break;
        case '-': k = TK_SUB; break;
        case '*': k = TK_MUL; break;
        case '/': k = TK_DIV; break;
        case '<':
            if (n == '=') { advance(s); k = TK_LE; }
            else k = TK_LT;
            break;
        case '>':
            if (n == '=') { advance(s); k = TK_GE; }
            else k = TK_GT;
            break;
        case '=':
            if (n == '=') { advance(s); k = TK_EQ; }
            else k = TK_ASG;
            break;
        case '!':
            if (n == '=') { advance(s); k = TK_NE; }
            else k = TK_NOT;
            break;
        case '&':
            if (n == '&') { advance(s); k = TK_AND; }
            else k = TK_ERR;
            break;
        case '|':
            if (n == '|') { advance(s); k = TK_OR; }
            else k = TK_ERR;
            break;
        case '(': k = TK_LPAR; break;
        case ')': k = TK_RPAR; break;
        case '[': k = TK_LBK; break;
        case ']': k = TK_RBK; break;
        case '{': k = TK_LBR; break;
        case '}': k = TK_RBR; break;
        case ',': k = TK_CMA; break;
        case ':': k = TK_COL; break;
        case ';': k = TK_SCO; break;
        default:  k = TK_ERR; break;
    }

    emit_lexeme(s, start, out);
    out->line = start_line;
    out->col = start_col;
    if (k == TK_ERR) {
        s->error_count++;
        fprintf(stderr, "[ERROR] illegal char '%s' at %d:%d, skipped\n",
                out->lexeme, start_line, start_col);
        return scanner_next(s, out);
    }
    out->kind = k;
    return true;
}
