#include "semantic.h"

#include <stdio.h>
#include <string.h>

/* 当前函数上下文：检查 return 类型 / void 函数禁止带表达式返回 */
typedef struct {
    BaseType ret_type;
    char     name[AST_MAX_NAME];
    bool     is_void;
} FuncCtx;

/* 把字符串运算符（如 "ADD"、"LT" 等）映射到结果类型规则 */
static bool is_relop(const char *op) {
    return strcmp(op, "LT") == 0 || strcmp(op, "LE") == 0 ||
           strcmp(op, "EQ") == 0 || strcmp(op, "GT") == 0 ||
           strcmp(op, "GE") == 0 || strcmp(op, "NE") == 0;
}

static BaseType promote(BaseType a, BaseType b) {
    if (a == TYPE_ERROR || b == TYPE_ERROR) return TYPE_ERROR;
    if (a == TYPE_FLOAT || b == TYPE_FLOAT) return TYPE_FLOAT;
    return TYPE_INT;
}

static bool compatible(BaseType lhs, BaseType rhs) {
    if (lhs == TYPE_ERROR || rhs == TYPE_ERROR) return true;
    if (lhs == rhs) return true;
    /* int → float 隐式提升允许，反向需要报警告但这里宽容处理 */
    if (lhs == TYPE_FLOAT && rhs == TYPE_INT) return true;
    if (lhs == TYPE_INT && rhs == TYPE_FLOAT) return true;
    return false;
}

/* 前向声明 */
static BaseType visit(AstNode *n, SymTab *st, ErrList *errs, FuncCtx *fc);

static void check_var_decl(AstNode *n, SymTab *st, ErrList *errs) {
    if (symtab_lookup_local(st, n->name)) {
        err_list_push(errs, ERR_SEM, n->line, n->col,
                      "redeclaration of '%s'", n->name);
        return;
    }
    Symbol s = (Symbol){0};
    strncpy(s.name, n->name, AST_MAX_NAME - 1);
    s.type      = n->type;
    s.is_array  = n->is_array;
    s.def_line  = n->line;
    s.def_col   = n->col;
    s.decl_node = n;
    symtab_insert(st, &s);
}

static void register_func(AstNode *fn, SymTab *st, ErrList *errs) {
    if (symtab_lookup_local(st, fn->name)) {
        err_list_push(errs, ERR_SEM, fn->line, fn->col,
                      "redeclaration of function '%s'", fn->name);
        return;
    }
    Symbol s = (Symbol){0};
    strncpy(s.name, fn->name, AST_MAX_NAME - 1);
    s.type     = fn->type;
    s.is_func  = true;
    s.def_line = fn->line;
    s.def_col  = fn->col;
    s.decl_node = fn;
    /* 找到参数列表（如果存在）以便记录形参签名 */
    for (int i = 0; i < fn->child_count; i++) {
        AstNode *c = fn->children[i];
        if (c->kind == AST_LIST) {
            int n = c->child_count > SYM_MAX_PARAMS ? SYM_MAX_PARAMS : c->child_count;
            s.param_count = n;
            for (int k = 0; k < n; k++) {
                AstNode *p = c->children[k];
                s.param_types[k]    = p->type;
                s.param_is_array[k] = p->is_array;
            }
            break;
        }
    }
    symtab_insert(st, &s);
}

static BaseType visit_call(AstNode *n, SymTab *st, ErrList *errs, FuncCtx *fc) {
    /* 先递归处理所有实参的类型推断（即使函数未声明也要走完） */
    BaseType arg_types[SYM_MAX_PARAMS];
    int arg_count = n->child_count;
    for (int i = 0; i < n->child_count && i < SYM_MAX_PARAMS; i++) {
        arg_types[i] = visit(n->children[i], st, errs, fc);
    }

    Symbol *sym = symtab_lookup(st, n->name);
    if (!sym) {
        err_list_push(errs, ERR_SEM, n->line, n->col,
                      "undefined function '%s'", n->name);
        n->type = TYPE_ERROR;
        return TYPE_ERROR;
    }
    if (!sym->is_func) {
        err_list_push(errs, ERR_SEM, n->line, n->col,
                      "'%s' is not a function", n->name);
        n->type = TYPE_ERROR;
        return TYPE_ERROR;
    }
    if (arg_count != sym->param_count) {
        err_list_push(errs, ERR_SEM, n->line, n->col,
                      "function '%s' expects %d argument(s), got %d",
                      n->name, sym->param_count, arg_count);
    } else {
        for (int i = 0; i < arg_count; i++) {
            if (!compatible(sym->param_types[i], arg_types[i])) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "argument %d of '%s': expected %s, got %s",
                              i + 1, n->name,
                              base_type_name(sym->param_types[i]),
                              base_type_name(arg_types[i]));
            }
        }
    }
    n->type = sym->type;
    return sym->type;
}

static BaseType visit(AstNode *n, SymTab *st, ErrList *errs, FuncCtx *fc) {
    if (!n) return TYPE_VOID;

    switch (n->kind) {
        case AST_PROGRAM:
        case AST_LIST: {
            /* 第一遍：把所有顶层函数声明先注册（前向引用支持） */
            if (n->kind == AST_PROGRAM) {
                for (int i = 0; i < n->child_count; i++) {
                    AstNode *c = n->children[i];
                    if (c->kind == AST_FUNC_DECL) register_func(c, st, errs);
                }
            }
            for (int i = 0; i < n->child_count; i++)
                visit(n->children[i], st, errs, fc);
            return TYPE_VOID;
        }

        case AST_FUNC_DECL: {
            FuncCtx my = {0};
            my.ret_type = n->type;
            my.is_void  = (n->type == TYPE_VOID);
            strncpy(my.name, n->name, AST_MAX_NAME - 1);
            symtab_enter_scope(st);
            /* 把形参插入函数 scope */
            for (int i = 0; i < n->child_count; i++) {
                AstNode *c = n->children[i];
                if (c->kind == AST_LIST) {
                    for (int k = 0; k < c->child_count; k++) {
                        AstNode *p = c->children[k];
                        if (symtab_lookup_local(st, p->name)) {
                            err_list_push(errs, ERR_SEM, p->line, p->col,
                                          "redeclaration of parameter '%s'", p->name);
                            continue;
                        }
                        Symbol s = (Symbol){0};
                        strncpy(s.name, p->name, AST_MAX_NAME - 1);
                        s.type     = p->type;
                        s.is_array = p->is_array;
                        s.def_line = p->line;
                        s.def_col  = p->col;
                        s.decl_node = p;
                        symtab_insert(st, &s);
                    }
                } else if (c->kind == AST_BLOCK) {
                    /* 进入函数体已经在我们 enter_scope 里了，BLOCK visit 不再单独开 scope */
                    for (int s = 0; s < c->child_count; s++)
                        visit(c->children[s], st, errs, &my);
                }
            }
            symtab_exit_scope(st);
            return TYPE_VOID;
        }

        case AST_VAR_DECL:
            check_var_decl(n, st, errs);
            return TYPE_VOID;

        case AST_BLOCK: {
            symtab_enter_scope(st);
            for (int i = 0; i < n->child_count; i++)
                visit(n->children[i], st, errs, fc);
            symtab_exit_scope(st);
            return TYPE_VOID;
        }

        case AST_ASSIGN: {
            /* children[0] 是 REF（普通变量）或 INDEX（数组元素），children[1] 是 rhs Expr */
            AstNode *lhs = n->children[0];
            AstNode *rhs = (n->child_count > 1) ? n->children[1] : NULL;
            BaseType lhs_t = visit(lhs, st, errs, fc);
            BaseType rhs_t = rhs ? visit(rhs, st, errs, fc) : TYPE_VOID;
            if (lhs_t != TYPE_ERROR && rhs_t != TYPE_ERROR && !compatible(lhs_t, rhs_t)) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "type mismatch in assignment to '%s': %s = %s",
                              n->name, base_type_name(lhs_t), base_type_name(rhs_t));
            }
            n->type = lhs_t;
            return lhs_t;
        }

        case AST_IF: {
            visit(n->children[0], st, errs, fc); /* cond */
            visit(n->children[1], st, errs, fc); /* then */
            if (n->child_count > 2)
                visit(n->children[2], st, errs, fc); /* else */
            return TYPE_VOID;
        }

        case AST_WHILE:
            visit(n->children[0], st, errs, fc);
            visit(n->children[1], st, errs, fc);
            return TYPE_VOID;

        case AST_RETURN: {
            BaseType rt = TYPE_VOID;
            if (n->child_count > 0) rt = visit(n->children[0], st, errs, fc);
            if (!fc) return TYPE_VOID;
            if (fc->is_void && n->child_count > 0) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "function '%s' is void but returns a value",
                              fc->name);
            } else if (!fc->is_void && n->child_count == 0) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "function '%s' must return %s",
                              fc->name, base_type_name(fc->ret_type));
            } else if (!fc->is_void && !compatible(fc->ret_type, rt)) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "return type of '%s' is %s but got %s",
                              fc->name, base_type_name(fc->ret_type),
                              base_type_name(rt));
            }
            return TYPE_VOID;
        }

        case AST_CALL:
            return visit_call(n, st, errs, fc);

        case AST_REF: {
            Symbol *s = symtab_lookup(st, n->name);
            if (!s) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "use of undeclared identifier '%s'", n->name);
                n->type = TYPE_ERROR;
                return TYPE_ERROR;
            }
            n->type = s->type;
            n->is_array = s->is_array;
            return s->type;
        }

        case AST_INDEX: {
            Symbol *s = symtab_lookup(st, n->name);
            if (!s) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "use of undeclared array '%s'", n->name);
                n->type = TYPE_ERROR;
                return TYPE_ERROR;
            }
            if (!s->is_array) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "'%s' is not an array", n->name);
            }
            /* 检查下标表达式 */
            if (n->child_count > 1) {
                BaseType idx_t = visit(n->children[1], st, errs, fc);
                if (idx_t != TYPE_ERROR && idx_t != TYPE_INT) {
                    err_list_push(errs, ERR_SEM, n->line, n->col,
                                  "array index of '%s' must be int, got %s",
                                  n->name, base_type_name(idx_t));
                }
            }
            n->type = s->type;
            return s->type;
        }

        case AST_BIN_OP: {
            BaseType a = visit(n->children[0], st, errs, fc);
            BaseType b = visit(n->children[1], st, errs, fc);
            n->type = promote(a, b);
            return n->type;
        }

        case AST_REL_OP: {
            visit(n->children[0], st, errs, fc);
            visit(n->children[1], st, errs, fc);
            n->type = TYPE_BOOL;
            return TYPE_BOOL;
        }

        case AST_PRINT:
        case AST_INPUT: {
            Symbol *s = symtab_lookup(st, n->name);
            if (!s) {
                err_list_push(errs, ERR_SEM, n->line, n->col,
                              "use of undeclared identifier '%s'", n->name);
            }
            return TYPE_VOID;
        }

        case AST_LIT_INT:   n->type = TYPE_INT;   return TYPE_INT;
        case AST_LIT_FLOAT: n->type = TYPE_FLOAT; return TYPE_FLOAT;
        case AST_TYPE:      return n->type;
        case AST_PARAM:     return n->type;
        case AST_UNARY_OP:  return visit(n->children[0], st, errs, fc);

        case AST_ERROR:
            return TYPE_ERROR;
    }
    /* 兜底：访问全部子节点 */
    for (int i = 0; i < n->child_count; i++)
        visit(n->children[i], st, errs, fc);
    return TYPE_VOID;
    (void)is_relop;
}

void semantic_check(AstNode *root, SymTab *st, ErrList *errs) {
    visit(root, st, errs, NULL);
}
