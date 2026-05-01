#ifndef TOKEN_H
#define TOKEN_H

#define TOKEN_LEXEME_LIMIT 128

typedef enum {
    TK_ERR = 0,
    TK_EOF,

    TK_INT, TK_FLOAT_KW, TK_VOID, TK_IF, TK_ELSE,
    TK_WHILE, TK_RETURN, TK_INPUT, TK_PRINT,

    TK_ID, TK_NUM, TK_FLOAT_LIT,

    TK_ADD, TK_SUB, TK_MUL, TK_DIV,

    TK_LT, TK_LE, TK_EQ, TK_GT, TK_GE, TK_NE,

    TK_AND, TK_OR, TK_NOT,

    TK_ASG, TK_AAS, TK_AAA,

    TK_LPAR, TK_RPAR, TK_LBK, TK_RBK, TK_LBR, TK_RBR,
    TK_CMA, TK_COL, TK_SCO, TK_DOT
} TokenKind;

typedef struct {
    TokenKind kind;
    char lexeme[TOKEN_LEXEME_LIMIT];
    int line;
    int col;
} Token;

const char *token_name(TokenKind k);

#endif
