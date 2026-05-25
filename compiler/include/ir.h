#ifndef IR_H
#define IR_H

#include "ast.h"
#include "symtab.h"

#include <stdio.h>

#define QUAD_FIELD_LEN     48
#define QUAD_INITIAL_CAP   64

typedef enum {
    OP_NOP = 0,
    OP_ASSIGN,                              /* (=, x, _, y) y = x */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,         /* (op, a, b, t) */
    OP_LT, OP_LE, OP_EQ, OP_GT, OP_GE, OP_NE,
    OP_LABEL,                               /* (LABEL, _, _, L) L: */
    OP_GOTO,                                /* (GOTO, _, _, L) */
    OP_IF_FALSE,                            /* (IF_FALSE, t, _, L) if (!t) goto L */
    OP_PARAM,                               /* (PARAM, x, _, _) */
    OP_CALL,                                /* (CALL, f, n, t) t = call f, n */
    OP_RETURN,                              /* (RETURN, x, _, _)  x=_ 表示无值 */
    OP_IDX_R,                               /* (=[], a, i, t) t = a[i] */
    OP_IDX_W,                               /* ([]=, x, i, a) a[i] = x */
    OP_PRINT,                               /* (PRINT, x, _, _) */
    OP_INPUT,                               /* (INPUT, _, _, x) */
    OP_FUNC_BEGIN,                          /* (FUNC_BEGIN, name, _, _) */
    OP_FUNC_END                             /* (FUNC_END, name, _, _) */
} QuadOp;

typedef struct {
    QuadOp op;
    char   arg1[QUAD_FIELD_LEN];
    char   arg2[QUAD_FIELD_LEN];
    char   result[QUAD_FIELD_LEN];
    int    line, col;
} Quad;

typedef struct {
    Quad *items;
    int   count;
    int   cap;
    int   next_temp;
    int   next_label;
} QuadList;

const char *quad_op_name(QuadOp op);
const char *quad_op_category(QuadOp op);   /* "arith" | "rel" | "ctrl" | "io" | "func" | "mem" | "assign" */

void quad_list_init(QuadList *q);
void quad_list_free(QuadList *q);

/* 主入口：遍历 AST 生成四元式；symtab 仅用于查类型与函数签名（必要时） */
void ir_generate(QuadList *q, AstNode *root, SymTab *st);

void ir_print(const QuadList *q, FILE *out);
void ir_to_json(const QuadList *q, FILE *out);

#endif
