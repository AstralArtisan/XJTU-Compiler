#include "token.h"

#include <stddef.h>

static const char *TOKEN_NAMES[] = {
    [TK_ERR] = "ERR", [TK_EOF] = "EOF",
    [TK_INT] = "INT", [TK_FLOAT_KW] = "FLOAT", [TK_VOID] = "VOID",
    [TK_IF] = "IF", [TK_ELSE] = "ELSE", [TK_WHILE] = "WHILE",
    [TK_RETURN] = "RETURN", [TK_INPUT] = "INPUT", [TK_PRINT] = "PRINT",
    [TK_ID] = "ID", [TK_NUM] = "NUM", [TK_FLOAT_LIT] = "FLOAT",
    [TK_ADD] = "ADD", [TK_SUB] = "SUB", [TK_MUL] = "MUL", [TK_DIV] = "DIV",
    [TK_LT] = "LT", [TK_LE] = "LE", [TK_EQ] = "EQ",
    [TK_GT] = "GT", [TK_GE] = "GE", [TK_NE] = "NE",
    [TK_AND] = "AND", [TK_OR] = "OR", [TK_NOT] = "NOT",
    [TK_ASG] = "ASG", [TK_AAS] = "AAS", [TK_AAA] = "AAA",
    [TK_LPAR] = "LPAR", [TK_RPAR] = "RPAR",
    [TK_LBK] = "LBK", [TK_RBK] = "RBK",
    [TK_LBR] = "LBR", [TK_RBR] = "RBR",
    [TK_CMA] = "CMA", [TK_COL] = "COL", [TK_SCO] = "SCO", [TK_DOT] = "DOT",
};

const char *token_name(TokenKind k) {
    if ((size_t)k >= sizeof(TOKEN_NAMES) / sizeof(TOKEN_NAMES[0])) return "ERR";
    const char *n = TOKEN_NAMES[k];
    return n ? n : "ERR";
}
