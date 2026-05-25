#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── op 名字 / 分类 ──────────────────────────────────── */

static const struct { QuadOp op; const char *name; const char *cat; } OPS[] = {
    { OP_NOP,        "NOP",        "ctrl"   },
    { OP_ASSIGN,     "ASSIGN",     "assign" },
    { OP_ADD,        "ADD",        "arith"  },
    { OP_SUB,        "SUB",        "arith"  },
    { OP_MUL,        "MUL",        "arith"  },
    { OP_DIV,        "DIV",        "arith"  },
    { OP_LT,         "LT",         "rel"    },
    { OP_LE,         "LE",         "rel"    },
    { OP_EQ,         "EQ",         "rel"    },
    { OP_GT,         "GT",         "rel"    },
    { OP_GE,         "GE",         "rel"    },
    { OP_NE,         "NE",         "rel"    },
    { OP_LABEL,      "LABEL",      "ctrl"   },
    { OP_GOTO,       "GOTO",       "ctrl"   },
    { OP_IF_FALSE,   "IF_FALSE",   "ctrl"   },
    { OP_PARAM,      "PARAM",      "func"   },
    { OP_CALL,       "CALL",       "func"   },
    { OP_RETURN,     "RETURN",     "ctrl"   },
    { OP_IDX_R,      "IDX_R",      "mem"    },
    { OP_IDX_W,      "IDX_W",      "mem"    },
    { OP_PRINT,      "PRINT",      "io"     },
    { OP_INPUT,      "INPUT",      "io"     },
    { OP_FUNC_BEGIN, "FUNC_BEGIN", "func"   },
    { OP_FUNC_END,   "FUNC_END",   "func"   }
};

const char *quad_op_name(QuadOp op) {
    for (size_t i = 0; i < sizeof(OPS)/sizeof(OPS[0]); i++)
        if (OPS[i].op == op) return OPS[i].name;
    return "?";
}

const char *quad_op_category(QuadOp op) {
    for (size_t i = 0; i < sizeof(OPS)/sizeof(OPS[0]); i++)
        if (OPS[i].op == op) return OPS[i].cat;
    return "ctrl";
}

/* ── QuadList ────────────────────────────────────────── */

void quad_list_init(QuadList *q) {
    q->items      = NULL;
    q->count      = 0;
    q->cap        = 0;
    q->next_temp  = 0;
    q->next_label = 0;
}

void quad_list_free(QuadList *q) {
    free(q->items);
    q->items = NULL;
    q->count = q->cap = 0;
}

static void emit(QuadList *q, QuadOp op,
                 const char *a1, const char *a2, const char *r,
                 int line, int col) {
    if (q->count >= q->cap) {
        int n = q->cap ? q->cap * 2 : QUAD_INITIAL_CAP;
        Quad *grown = realloc(q->items, (size_t)n * sizeof(Quad));
        if (!grown) return;
        q->items = grown;
        q->cap   = n;
    }
    Quad *qd = &q->items[q->count++];
    qd->op   = op;
    qd->line = line;
    qd->col  = col;
    snprintf(qd->arg1,   QUAD_FIELD_LEN, "%s", a1 && *a1 ? a1 : "_");
    snprintf(qd->arg2,   QUAD_FIELD_LEN, "%s", a2 && *a2 ? a2 : "_");
    snprintf(qd->result, QUAD_FIELD_LEN, "%s", r  && *r  ? r  : "_");
}

static void gen_temp(QuadList *q, char buf[QUAD_FIELD_LEN]) {
    snprintf(buf, QUAD_FIELD_LEN, "t%d", ++q->next_temp);
}

static void gen_label(QuadList *q, char buf[QUAD_FIELD_LEN]) {
    snprintf(buf, QUAD_FIELD_LEN, "L%d", ++q->next_label);
}

/* ── 翻译：表达式与语句 ──────────────────────────────── */

static void expr(QuadList *q, AstNode *n, SymTab *st, char out[QUAD_FIELD_LEN]);
static void stmt(QuadList *q, AstNode *n, SymTab *st);

static QuadOp bin_op_for(const char *name) {
    if (strcmp(name, "ADD") == 0) return OP_ADD;
    if (strcmp(name, "SUB") == 0) return OP_SUB;
    if (strcmp(name, "MUL") == 0) return OP_MUL;
    if (strcmp(name, "DIV") == 0) return OP_DIV;
    return OP_NOP;
}

static QuadOp rel_op_for(const char *name) {
    if (strcmp(name, "LT") == 0) return OP_LT;
    if (strcmp(name, "LE") == 0) return OP_LE;
    if (strcmp(name, "EQ") == 0) return OP_EQ;
    if (strcmp(name, "GT") == 0) return OP_GT;
    if (strcmp(name, "GE") == 0) return OP_GE;
    if (strcmp(name, "NE") == 0) return OP_NE;
    return OP_NOP;
}

static void expr(QuadList *q, AstNode *n, SymTab *st, char out[QUAD_FIELD_LEN]) {
    if (!n) { snprintf(out, QUAD_FIELD_LEN, "_"); return; }

    switch (n->kind) {
        case AST_LIT_INT:
            snprintf(out, QUAD_FIELD_LEN, "%d", n->int_val);
            return;
        case AST_LIT_FLOAT:
            /* 用 lex 字段保留原始字面量，避免 %g 重新格式化丢精度 */
            if (n->lex[0]) snprintf(out, QUAD_FIELD_LEN, "%s", n->lex);
            else           snprintf(out, QUAD_FIELD_LEN, "%g", n->float_val);
            return;
        case AST_REF:
            snprintf(out, QUAD_FIELD_LEN, "%s", n->name);
            return;
        case AST_BIN_OP: {
            char a[QUAD_FIELD_LEN], b[QUAD_FIELD_LEN];
            expr(q, n->children[0], st, a);
            expr(q, n->children[1], st, b);
            QuadOp op = bin_op_for(n->name);
            gen_temp(q, out);
            emit(q, op, a, b, out, n->line, n->col);
            return;
        }
        case AST_REL_OP: {
            char a[QUAD_FIELD_LEN], b[QUAD_FIELD_LEN];
            expr(q, n->children[0], st, a);
            expr(q, n->children[1], st, b);
            QuadOp op = rel_op_for(n->name);
            gen_temp(q, out);
            emit(q, op, a, b, out, n->line, n->col);
            return;
        }
        case AST_UNARY_OP: {
            /* 单目算术（一元负号等）：t = 0 op x，简化处理 */
            char x[QUAD_FIELD_LEN];
            expr(q, n->children[0], st, x);
            QuadOp op = bin_op_for(n->name);
            if (op == OP_NOP) op = OP_SUB;
            gen_temp(q, out);
            emit(q, op, "0", x, out, n->line, n->col);
            return;
        }
        case AST_INDEX: {
            /* a[i] 作为右值：t = a[i] */
            char i_addr[QUAD_FIELD_LEN];
            expr(q, n->children[1], st, i_addr);
            gen_temp(q, out);
            emit(q, OP_IDX_R, n->name, i_addr, out, n->line, n->col);
            return;
        }
        case AST_CALL: {
            /* 逐个 PARAM，然后 CALL f n t */
            int n_args = n->child_count;
            for (int i = 0; i < n_args; i++) {
                char a[QUAD_FIELD_LEN];
                expr(q, n->children[i], st, a);
                emit(q, OP_PARAM, a, NULL, NULL, n->line, n->col);
            }
            char n_str[QUAD_FIELD_LEN];
            snprintf(n_str, QUAD_FIELD_LEN, "%d", n_args);
            gen_temp(q, out);
            emit(q, OP_CALL, n->name, n_str, out, n->line, n->col);
            return;
        }
        default:
            snprintf(out, QUAD_FIELD_LEN, "_");
            return;
    }
}

static void stmt(QuadList *q, AstNode *n, SymTab *st) {
    if (!n) return;

    switch (n->kind) {
        case AST_PROGRAM:
        case AST_LIST:
        case AST_BLOCK:
            for (int i = 0; i < n->child_count; i++) stmt(q, n->children[i], st);
            return;

        case AST_FUNC_DECL: {
            emit(q, OP_FUNC_BEGIN, n->name, NULL, NULL, n->line, n->col);
            AstNode *body = NULL;
            for (int i = 0; i < n->child_count; i++) {
                AstNode *c = n->children[i];
                if (c && c->kind == AST_BLOCK) { body = c; break; }
            }
            if (body) stmt(q, body, st);
            /* void 函数尾如未显式 return，补一条 */
            bool has_return = false;
            if (body) {
                for (int i = body->child_count - 1; i >= 0; i--) {
                    if (body->children[i] && body->children[i]->kind == AST_RETURN) {
                        has_return = true; break;
                    }
                }
            }
            if (n->type == TYPE_VOID && !has_return) {
                emit(q, OP_RETURN, NULL, NULL, NULL, n->line, n->col);
            }
            emit(q, OP_FUNC_END, n->name, NULL, NULL, n->line, n->col);
            return;
        }

        case AST_VAR_DECL:
        case AST_PARAM:
        case AST_TYPE:
        case AST_ERROR:
            return;   /* 不产生四元式 */

        case AST_ASSIGN: {
            /* children[0] 是 REF 或 INDEX；children[1] 是 rhs */
            AstNode *lhs = n->children[0];
            AstNode *rhs = (n->child_count > 1) ? n->children[1] : NULL;
            char r_addr[QUAD_FIELD_LEN] = "_";
            if (rhs) expr(q, rhs, st, r_addr);
            if (lhs && lhs->kind == AST_INDEX) {
                char i_addr[QUAD_FIELD_LEN];
                expr(q, lhs->children[1], st, i_addr);
                emit(q, OP_IDX_W, r_addr, i_addr, lhs->name, n->line, n->col);
            } else {
                const char *name = lhs ? lhs->name : n->name;
                emit(q, OP_ASSIGN, r_addr, NULL, name, n->line, n->col);
            }
            return;
        }

        case AST_IF: {
            AstNode *cond = n->children[0];
            AstNode *then_s = n->children[1];
            AstNode *else_s = (n->child_count > 2) ? n->children[2] : NULL;

            char t[QUAD_FIELD_LEN];
            expr(q, cond, st, t);

            char Lelse[QUAD_FIELD_LEN], Lend[QUAD_FIELD_LEN];
            if (else_s) {
                gen_label(q, Lelse);
                gen_label(q, Lend);
                emit(q, OP_IF_FALSE, t, NULL, Lelse, n->line, n->col);
                stmt(q, then_s, st);
                emit(q, OP_GOTO, NULL, NULL, Lend, n->line, n->col);
                emit(q, OP_LABEL, NULL, NULL, Lelse, n->line, n->col);
                stmt(q, else_s, st);
                emit(q, OP_LABEL, NULL, NULL, Lend, n->line, n->col);
            } else {
                gen_label(q, Lend);
                emit(q, OP_IF_FALSE, t, NULL, Lend, n->line, n->col);
                stmt(q, then_s, st);
                emit(q, OP_LABEL, NULL, NULL, Lend, n->line, n->col);
            }
            return;
        }

        case AST_WHILE: {
            AstNode *cond = n->children[0];
            AstNode *body = n->children[1];
            char Lstart[QUAD_FIELD_LEN], Lend[QUAD_FIELD_LEN];
            gen_label(q, Lstart);
            gen_label(q, Lend);
            emit(q, OP_LABEL, NULL, NULL, Lstart, n->line, n->col);
            char t[QUAD_FIELD_LEN];
            expr(q, cond, st, t);
            emit(q, OP_IF_FALSE, t, NULL, Lend, n->line, n->col);
            stmt(q, body, st);
            emit(q, OP_GOTO, NULL, NULL, Lstart, n->line, n->col);
            emit(q, OP_LABEL, NULL, NULL, Lend, n->line, n->col);
            return;
        }

        case AST_RETURN: {
            if (n->child_count > 0) {
                char t[QUAD_FIELD_LEN];
                expr(q, n->children[0], st, t);
                emit(q, OP_RETURN, t, NULL, NULL, n->line, n->col);
            } else {
                emit(q, OP_RETURN, NULL, NULL, NULL, n->line, n->col);
            }
            return;
        }

        case AST_CALL: {
            /* 语句位置的函数调用：不取返回值 */
            int n_args = n->child_count;
            for (int i = 0; i < n_args; i++) {
                char a[QUAD_FIELD_LEN];
                expr(q, n->children[i], st, a);
                emit(q, OP_PARAM, a, NULL, NULL, n->line, n->col);
            }
            char n_str[QUAD_FIELD_LEN];
            snprintf(n_str, QUAD_FIELD_LEN, "%d", n_args);
            emit(q, OP_CALL, n->name, n_str, NULL, n->line, n->col);
            return;
        }

        case AST_PRINT:
            emit(q, OP_PRINT, n->name, NULL, NULL, n->line, n->col);
            return;

        case AST_INPUT:
            emit(q, OP_INPUT, NULL, NULL, n->name, n->line, n->col);
            return;

        default:
            /* 表达式节点不应出现在 stmt 位置；走 expr 兜底丢弃结果 */
            { char tmp[QUAD_FIELD_LEN]; expr(q, n, st, tmp); }
            return;
    }
}

void ir_generate(QuadList *q, AstNode *root, SymTab *st) {
    if (!root) return;
    stmt(q, root, st);
}

/* ── 文本输出 ────────────────────────────────────────── */

void ir_print(const QuadList *q, FILE *out) {
    for (int i = 0; i < q->count; i++) {
        const Quad *qd = &q->items[i];
        fprintf(out, "%4d:  (%s, %s, %s, %s)\n",
                i + 1, quad_op_name(qd->op), qd->arg1, qd->arg2, qd->result);
    }
}

/* ── JSON 输出 ───────────────────────────────────────── */

static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:   fputc(*s, out);     break;
        }
    }
    fputc('"', out);
}

void ir_to_json(const QuadList *q, FILE *out) {
    fputc('[', out);
    for (int i = 0; i < q->count; i++) {
        if (i) fputc(',', out);
        const Quad *qd = &q->items[i];
        fprintf(out, "{\"i\":%d,\"op\":", i + 1);
        json_str(out, quad_op_name(qd->op));
        fputs(",\"cat\":", out);
        json_str(out, quad_op_category(qd->op));
        fputs(",\"arg1\":", out);
        json_str(out, qd->arg1);
        fputs(",\"arg2\":", out);
        json_str(out, qd->arg2);
        fputs(",\"result\":", out);
        json_str(out, qd->result);
        fprintf(out, ",\"line\":%d,\"col\":%d}", qd->line, qd->col);
    }
    fputc(']', out);
}
