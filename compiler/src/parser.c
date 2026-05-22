#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── trace 数据结构 ──────────────────────────────────── */

#define TRACE_MAX_STEPS       4096
#define TRACE_STACK_SNAPSHOT  64

typedef enum {
    TA_ERROR  = 0,
    TA_SHIFT  = 1,
    TA_REDUCE = 2,
    TA_ACCEPT = 3
} TraceActionKind;

typedef struct {
    int             step;
    int             input_pos;
    TraceActionKind action;
    int             target;             /* shift 目标状态 / reduce 产生式编号 */
    int             prod_id;            /* reduce 时有效；其它 -1 */
    int             stack_depth;
    int             stack_states[TRACE_STACK_SNAPSHOT];
    int             stack_syms[TRACE_STACK_SNAPSHOT];  /* grammar symbol id 或 -1 */
    int             lookahead_sym;      /* 当前 lookahead 的 grammar symbol id */
    int             new_ast_id;         /* reduce 后新生成 AST 节点的 id；其它 -1 */
} TraceStep;

struct ParseTrace {
    TraceStep steps[TRACE_MAX_STEPS];
    int       step_count;
};

ParseTrace *parse_trace_new(void) {
    ParseTrace *t = calloc(1, sizeof(*t));
    return t;
}

void parse_trace_free(ParseTrace *t) { free(t); }

static void trace_push(ParseTrace *t, Parser *p,
                       TraceActionKind kind, int target, int prod_id,
                       int input_pos, int lookahead_sym, int new_ast_id) {
    if (!t || t->step_count >= TRACE_MAX_STEPS) return;
    TraceStep *s = &t->steps[t->step_count];
    s->step      = t->step_count;
    s->input_pos = input_pos;
    s->action    = kind;
    s->target    = target;
    s->prod_id   = prod_id;
    s->lookahead_sym = lookahead_sym;
    s->new_ast_id    = new_ast_id;
    int n = p->sp + 1;
    if (n > TRACE_STACK_SNAPSHOT) n = TRACE_STACK_SNAPSHOT;
    s->stack_depth = n;
    for (int i = 0; i < n; i++) {
        s->stack_states[i] = p->states[i];
        s->stack_syms[i] = -1;
    }
    t->step_count++;
}

void parse_trace_to_json(const ParseTrace *t, FILE *out) {
    if (!t) { fputs("[]", out); return; }
    fputc('[', out);
    for (int i = 0; i < t->step_count; i++) {
        if (i) fputc(',', out);
        const TraceStep *s = &t->steps[i];
        const char *ak =
            s->action == TA_SHIFT  ? "shift"  :
            s->action == TA_REDUCE ? "reduce" :
            s->action == TA_ACCEPT ? "accept" : "error";
        fprintf(out, "{\"step\":%d,\"input_pos\":%d,\"action\":\"%s\",\"target\":%d",
                s->step, s->input_pos, ak, s->target);
        if (s->prod_id >= 0) fprintf(out, ",\"prod\":%d", s->prod_id);
        if (s->lookahead_sym >= 0) fprintf(out, ",\"lookahead\":%d", s->lookahead_sym);
        if (s->new_ast_id >= 0) fprintf(out, ",\"new_ast\":%d", s->new_ast_id);
        fprintf(out, ",\"stack\":[");
        for (int k = 0; k < s->stack_depth; k++) {
            if (k) fputc(',', out);
            fprintf(out, "%d", s->stack_states[k]);
        }
        fputs("]}", out);
    }
    fputc(']', out);
}

/* ── 静态工具 ────────────────────────────────────────── */

static int strieq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static const char *prod_lhs(const Parser *p, int prod_id) {
    return grammar_symbol_name(p->grammar, p->grammar->productions[prod_id].lhs);
}

static const char *prod_rhs_name(const Parser *p, int prod_id, int idx) {
    const GrProduction *pr = &p->grammar->productions[prod_id];
    if (idx >= pr->rhs_len) return "";
    return grammar_symbol_name(p->grammar, pr->rhs[idx]);
}

/* 把 token kind 映射到 grammar 中的终结符 id；返回 -1 表示未定义 */
static int term_id(Parser *p, TokenKind k) {
    if ((int)k < 0 || (int)k >= 64) return -1;
    return p->term_id_by_token[k];
}

/* ── 终端节点构造 ────────────────────────────────────── */

static AstNode *terminal_to_ast(Parser *p, const Token *t) {
    switch (t->kind) {
        case TK_ID: {
            AstNode *n = ast_new(p->arena, AST_REF, t->line, t->col);
            strncpy(n->name, t->lexeme, AST_MAX_NAME - 1);
            return n;
        }
        case TK_NUM: {
            AstNode *n = ast_new(p->arena, AST_LIT_INT, t->line, t->col);
            n->int_val = atoi(t->lexeme);
            n->type = TYPE_INT;
            strncpy(n->lex, t->lexeme, AST_LEX_LIMIT - 1);
            return n;
        }
        case TK_FLOAT_LIT: {
            AstNode *n = ast_new(p->arena, AST_LIT_FLOAT, t->line, t->col);
            n->float_val = atof(t->lexeme);
            n->type = TYPE_FLOAT;
            strncpy(n->lex, t->lexeme, AST_LEX_LIMIT - 1);
            return n;
        }
        case TK_INT: {
            AstNode *n = ast_new(p->arena, AST_TYPE, t->line, t->col);
            n->type = TYPE_INT; strncpy(n->name, "int", AST_MAX_NAME - 1);
            return n;
        }
        case TK_FLOAT_KW: {
            AstNode *n = ast_new(p->arena, AST_TYPE, t->line, t->col);
            n->type = TYPE_FLOAT; strncpy(n->name, "float", AST_MAX_NAME - 1);
            return n;
        }
        case TK_VOID: {
            AstNode *n = ast_new(p->arena, AST_TYPE, t->line, t->col);
            n->type = TYPE_VOID; strncpy(n->name, "void", AST_MAX_NAME - 1);
            return n;
        }
        default: {
            /* 关键字/界符/运算符：占位节点，仅承载位置与 lexeme，便于 reduce 时读取 */
            AstNode *n = ast_new(p->arena, AST_ERROR, t->line, t->col);
            n->kind = AST_ERROR;
            strncpy(n->name, token_name(t->kind), AST_MAX_NAME - 1);
            strncpy(n->lex, t->lexeme, AST_LEX_LIMIT - 1);
            return n;
        }
    }
}

/* ── reduce 动作分发 ─────────────────────────────────── */

static AstNode *dispatch_reduce(Parser *p, int prod_id, AstNode **rhs, int n) {
    const char *lhs = prod_lhs(p, prod_id);

    if (strieq(lhs, "P")) {
        /* P -> Decls：把 Decls 容器升级为 PROGRAM */
        AstNode *prog = rhs[0];
        if (prog) prog->kind = AST_PROGRAM;
        return prog;
    }

    if (strieq(lhs, "Decls")) {
        if (n == 1) {
            /* Decls -> Decl */
            AstNode *list = ast_new(p->arena, AST_LIST, rhs[0]->line, rhs[0]->col);
            ast_add_child(list, rhs[0]);
            return list;
        }
        /* Decls -> Decls Decl */
        ast_add_child(rhs[0], rhs[1]);
        return rhs[0];
    }

    if (strieq(lhs, "Decl")) {
        return rhs[0]; /* passthrough；对于 VarDecl SCO 形态 n==2，丢弃 SCO */
    }

    if (strieq(lhs, "VarDecl")) {
        /* VarDecl -> Type ID | Type ID LBK NUM RBK */
        AstNode *type_node = rhs[0];
        AstNode *id_node   = rhs[1];
        AstNode *decl = ast_new(p->arena, AST_VAR_DECL, type_node->line, type_node->col);
        decl->type = type_node->type;
        strncpy(decl->name, id_node->name, AST_MAX_NAME - 1);
        if (n == 5) {
            decl->is_array = true;
            decl->array_size = rhs[3]->int_val;
        }
        return decl;
    }

    if (strieq(lhs, "Type")) {
        return rhs[0]; /* AST_TYPE 节点已由 terminal_to_ast 构造 */
    }

    if (strieq(lhs, "FuncDef")) {
        /* FuncDef -> Type ID LPAR ParamList RPAR Body SCO
                    | Type ID LPAR RPAR Body SCO */
        AstNode *type_node = rhs[0];
        AstNode *id_node   = rhs[1];
        AstNode *params    = (n == 7) ? rhs[3] : NULL;
        AstNode *body      = (n == 7) ? rhs[5] : rhs[4];
        AstNode *func = ast_new(p->arena, AST_FUNC_DECL, type_node->line, type_node->col);
        func->type = type_node->type;
        strncpy(func->name, id_node->name, AST_MAX_NAME - 1);
        ast_add_child(func, type_node);
        if (params) ast_add_child(func, params);
        if (body)   ast_add_child(func, body);
        return func;
    }

    if (strieq(lhs, "ParamList")) {
        if (n == 1) {
            AstNode *list = ast_new(p->arena, AST_LIST, rhs[0]->line, rhs[0]->col);
            ast_add_child(list, rhs[0]);
            return list;
        }
        ast_add_child(rhs[0], rhs[1]);
        return rhs[0];
    }

    if (strieq(lhs, "Param")) {
        /* Param -> Type ID SCO | Type ID LBK RBK SCO */
        AstNode *type_node = rhs[0];
        AstNode *id_node   = rhs[1];
        AstNode *param = ast_new(p->arena, AST_PARAM, type_node->line, type_node->col);
        param->type = type_node->type;
        strncpy(param->name, id_node->name, AST_MAX_NAME - 1);
        if (n == 5) param->is_array = true;
        return param;
    }

    if (strieq(lhs, "Body") || strieq(lhs, "Block")) {
        /* Body -> LBR StmtList RBR ；Block 同形 */
        AstNode *stmts = rhs[1];
        AstNode *blk = ast_new(p->arena, AST_BLOCK, rhs[0]->line, rhs[0]->col);
        if (stmts) {
            /* 把 StmtList 容器的 children 直接搬到 BLOCK 下 */
            for (int i = 0; i < stmts->child_count; i++) {
                ast_add_child(blk, stmts->children[i]);
            }
        }
        return blk;
    }

    if (strieq(lhs, "StmtList")) {
        if (n == 1) {
            AstNode *list = ast_new(p->arena, AST_LIST, rhs[0]->line, rhs[0]->col);
            ast_add_child(list, rhs[0]);
            return list;
        }
        /* StmtList SCO Stmt */
        ast_add_child(rhs[0], rhs[2]);
        return rhs[0];
    }

    if (strieq(lhs, "Stmt") || strieq(lhs, "Matched") || strieq(lhs, "Unmatched")
        || strieq(lhs, "OtherStmt")) {
        /* 这几条都是 passthrough；只有 Matched/Unmatched 的 IF 分支例外 */
        if (n >= 7 && strieq(prod_rhs_name(p, prod_id, 0), "IF")) {
            /* IF LPAR B RPAR S1 [ELSE S2] */
            AstNode *if_node = ast_new(p->arena, AST_IF, rhs[0]->line, rhs[0]->col);
            ast_add_child(if_node, rhs[2]); /* condition B */
            ast_add_child(if_node, rhs[4]); /* then branch */
            if (n == 7) ast_add_child(if_node, rhs[6]); /* else branch */
            return if_node;
        }
        if (n >= 5 && strieq(prod_rhs_name(p, prod_id, 0), "IF")) {
            /* IF LPAR B RPAR Stmt（无 else） */
            AstNode *if_node = ast_new(p->arena, AST_IF, rhs[0]->line, rhs[0]->col);
            ast_add_child(if_node, rhs[2]);
            ast_add_child(if_node, rhs[4]);
            return if_node;
        }
        return rhs[0];
    }

    if (strieq(lhs, "Assign")) {
        /* Assign -> ID ASG Expr | ID LBK Expr RBK ASG Expr */
        AstNode *id_node = rhs[0];
        AstNode *assign = ast_new(p->arena, AST_ASSIGN, id_node->line, id_node->col);
        strncpy(assign->name, id_node->name, AST_MAX_NAME - 1);
        if (n == 3) {
            ast_add_child(assign, id_node);
            ast_add_child(assign, rhs[2]);
        } else {
            /* ID LBK Expr RBK ASG Expr */
            AstNode *idx = ast_new(p->arena, AST_INDEX, id_node->line, id_node->col);
            strncpy(idx->name, id_node->name, AST_MAX_NAME - 1);
            ast_add_child(idx, id_node);
            ast_add_child(idx, rhs[2]);
            ast_add_child(assign, idx);
            ast_add_child(assign, rhs[5]);
        }
        return assign;
    }

    if (strieq(lhs, "WhileStmt")) {
        AstNode *w = ast_new(p->arena, AST_WHILE, rhs[0]->line, rhs[0]->col);
        ast_add_child(w, rhs[2]); /* cond */
        ast_add_child(w, rhs[4]); /* body */
        return w;
    }

    if (strieq(lhs, "ReturnStmt")) {
        AstNode *r = ast_new(p->arena, AST_RETURN, rhs[0]->line, rhs[0]->col);
        if (n == 2) ast_add_child(r, rhs[1]);
        return r;
    }

    if (strieq(lhs, "Call")) {
        AstNode *id_node = rhs[0];
        AstNode *call = ast_new(p->arena, AST_CALL, id_node->line, id_node->col);
        strncpy(call->name, id_node->name, AST_MAX_NAME - 1);
        if (n == 4) {
            /* ID LPAR ArgList RPAR */
            AstNode *args = rhs[2];
            for (int i = 0; i < args->child_count; i++)
                ast_add_child(call, args->children[i]);
        }
        return call;
    }

    if (strieq(lhs, "PrintStmt")) {
        AstNode *node = ast_new(p->arena, AST_PRINT, rhs[0]->line, rhs[0]->col);
        strncpy(node->name, rhs[1]->name, AST_MAX_NAME - 1);
        return node;
    }
    if (strieq(lhs, "InputStmt")) {
        AstNode *node = ast_new(p->arena, AST_INPUT, rhs[0]->line, rhs[0]->col);
        strncpy(node->name, rhs[1]->name, AST_MAX_NAME - 1);
        return node;
    }

    if (strieq(lhs, "ArgList")) {
        if (n == 1) {
            AstNode *list = ast_new(p->arena, AST_LIST, rhs[0]->line, rhs[0]->col);
            ast_add_child(list, rhs[0]);
            return list;
        }
        /* ArgList CMA Expr */
        ast_add_child(rhs[0], rhs[2]);
        return rhs[0];
    }

    if (strieq(lhs, "B")) {
        if (n == 1) return rhs[0];
        /* B -> Expr op Expr */
        AstNode *rel = ast_new(p->arena, AST_REL_OP, rhs[1]->line, rhs[1]->col);
        strncpy(rel->name, prod_rhs_name(p, prod_id, 1), AST_MAX_NAME - 1);
        ast_add_child(rel, rhs[0]);
        ast_add_child(rel, rhs[2]);
        rel->type = TYPE_BOOL;
        return rel;
    }

    if (strieq(lhs, "Expr") || strieq(lhs, "Term")) {
        if (n == 1) return rhs[0];
        AstNode *op = ast_new(p->arena, AST_BIN_OP, rhs[1]->line, rhs[1]->col);
        strncpy(op->name, prod_rhs_name(p, prod_id, 1), AST_MAX_NAME - 1);
        ast_add_child(op, rhs[0]);
        ast_add_child(op, rhs[2]);
        return op;
    }

    if (strieq(lhs, "Factor")) {
        if (n == 1) return rhs[0];
        if (n == 3) {
            const char *r0 = prod_rhs_name(p, prod_id, 0);
            if (strieq(r0, "LPAR")) return rhs[1]; /* ( Expr ) */
            if (strieq(r0, "ID")) {
                /* ID LPAR RPAR */
                AstNode *call = ast_new(p->arena, AST_CALL, rhs[0]->line, rhs[0]->col);
                strncpy(call->name, rhs[0]->name, AST_MAX_NAME - 1);
                return call;
            }
        }
        if (n == 4) {
            const char *r0 = prod_rhs_name(p, prod_id, 0);
            const char *r1 = prod_rhs_name(p, prod_id, 1);
            if (strieq(r0, "ID") && strieq(r1, "LPAR")) {
                /* ID LPAR ArgList RPAR */
                AstNode *call = ast_new(p->arena, AST_CALL, rhs[0]->line, rhs[0]->col);
                strncpy(call->name, rhs[0]->name, AST_MAX_NAME - 1);
                AstNode *args = rhs[2];
                for (int i = 0; i < args->child_count; i++)
                    ast_add_child(call, args->children[i]);
                return call;
            }
            if (strieq(r0, "ID") && strieq(r1, "LBK")) {
                /* ID LBK Expr RBK */
                AstNode *idx = ast_new(p->arena, AST_INDEX, rhs[0]->line, rhs[0]->col);
                strncpy(idx->name, rhs[0]->name, AST_MAX_NAME - 1);
                ast_add_child(idx, rhs[0]);
                ast_add_child(idx, rhs[2]);
                return idx;
            }
        }
    }

    /* 默认：passthrough 第一个非 NULL 子节点 */
    for (int i = 0; i < n; i++) if (rhs[i]) return rhs[i];
    return NULL;
}

/* ── parser_init ─────────────────────────────────────── */

void parser_init(Parser *p, AstArena *arena, SymTab *symtab, ErrList *errors,
                 const Grammar *g, const SlrTable *slr) {
    memset(p, 0, sizeof(*p));
    p->arena   = arena;
    p->symtab  = symtab;
    p->errors  = errors;
    p->grammar = g;
    p->slr     = slr;

    for (int i = 0; i < 64; i++) p->term_id_by_token[i] = -1;
    /* 按 token_name 查 grammar symbol id */
    for (int k = TK_ERR; k <= TK_DOT; k++) {
        const char *name = token_name((TokenKind)k);
        if (!name) continue;
        int id = grammar_lookup(g, name);
        if (id >= 0) p->term_id_by_token[k] = id;
    }
}

/* ── 主循环 ──────────────────────────────────────────── */

static int find_goto(const SlrTable *t, int state, int sym) {
    return t->goto_tab[state][sym];
}

static const SlrAction *action_at(const SlrTable *t, int state, int sym) {
    if (sym < 0) return NULL;
    return &t->action[state][sym];
}

AstNode *parser_run(Parser *p, const Token *tokens, int token_count) {
    /* 初始化栈 */
    p->sp = 0;
    p->states[p->sp] = 0;
    p->attrs[p->sp]  = NULL;

    int pos = 0;
    int end_sym = p->grammar->end_symbol;

    while (pos <= token_count) {
        const Token *tok;
        Token eof_tok = { TK_EOF, "", 0, 0 };
        if (pos < token_count) tok = &tokens[pos];
        else                   tok = &eof_tok;

        int sym = (pos < token_count) ? term_id(p, tok->kind) : end_sym;
        if (sym < 0) {
            err_list_push(p->errors, ERR_LEX, tok->line, tok->col,
                          "unknown token kind '%s'", token_name(tok->kind));
            /* 跳过这个 token */
            pos++;
            continue;
        }

        int state = p->states[p->sp];
        const SlrAction *act = action_at(p->slr, state, sym);
        if (!act || act->kind == SLR_ACTION_ERROR) {
            err_list_push(p->errors, ERR_SYN, tok->line, tok->col,
                          "unexpected token '%s' ('%s')",
                          token_name(tok->kind), tok->lexeme);
            /* panic-mode：跳到下一个 SCO 或文件末，再 pop 状态直到能接受 SCO */
            while (pos < token_count && tokens[pos].kind != TK_SCO) pos++;
            if (pos >= token_count) return p->attrs[p->sp];
            /* 跳过 SCO 本身 */
            pos++;
            /* 重置状态栈到初态，重新开始下一段 */
            p->sp = 0;
            p->states[0] = 0;
            p->attrs[0] = NULL;
            continue;
        }

        if (act->kind == SLR_ACTION_SHIFT) {
            if (p->sp + 1 >= PARSER_STACK_CAP) {
                err_list_push(p->errors, ERR_SYN, tok->line, tok->col,
                              "parser stack overflow");
                return p->attrs[p->sp];
            }
            AstNode *term_node = terminal_to_ast(p, tok);
            p->sp++;
            p->states[p->sp] = act->target;
            p->attrs[p->sp]  = term_node;
            trace_push(p->trace, p, TA_SHIFT, act->target, -1, pos, sym, -1);
            pos++;
            continue;
        }

        if (act->kind == SLR_ACTION_REDUCE) {
            int prod_id = act->target;
            const GrProduction *pr = &p->grammar->productions[prod_id];
            int rhs_len = pr->rhs_len;

            /* 弹出 |β| 个属性 */
            AstNode *rhs[16];
            for (int i = 0; i < rhs_len; i++) {
                rhs[rhs_len - 1 - i] = p->attrs[p->sp];
                p->sp--;
            }
            AstNode *new_attr = dispatch_reduce(p, prod_id, rhs, rhs_len);

            /* GOTO[新栈顶][lhs] */
            int top = p->states[p->sp];
            int goto_state = find_goto(p->slr, top, pr->lhs);
            if (goto_state < 0) {
                err_list_push(p->errors, ERR_SYN, tok->line, tok->col,
                              "internal: missing GOTO[%d][%s]", top,
                              grammar_symbol_name(p->grammar, pr->lhs));
                return new_attr;
            }
            p->sp++;
            p->states[p->sp] = goto_state;
            p->attrs[p->sp]  = new_attr;
            trace_push(p->trace, p, TA_REDUCE, prod_id, prod_id, pos, sym,
                       new_attr ? new_attr->id : -1);
            continue;
        }

        if (act->kind == SLR_ACTION_ACCEPT) {
            trace_push(p->trace, p, TA_ACCEPT, 0, -1, pos, sym, -1);
            return p->attrs[p->sp];
        }
    }
    return p->attrs[p->sp];
}
