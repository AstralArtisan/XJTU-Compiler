#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

#include <stdbool.h>
#include <stdio.h>

#define SYM_MAX_PARAMS    8
#define SYM_MAX_SYMBOLS   128
#define SYM_MAX_SCOPES    32

typedef struct {
    BaseType type;
    bool     is_array;
    bool     is_func;
    char     name[AST_MAX_NAME];
    int      def_line, def_col;
    int      scope_level;
    int      param_count;
    BaseType param_types[SYM_MAX_PARAMS];
    bool     param_is_array[SYM_MAX_PARAMS];
    AstNode *decl_node;     /* 指向声明它的 AST 节点 */
} Symbol;

typedef struct {
    int     level;          /* 0 = 全局；嵌套块依次递增 */
    int     parent;         /* 父 scope 在 SymTab.scopes 数组中的索引；-1 = 根 */
    Symbol  symbols[SYM_MAX_SYMBOLS];
    int     symbol_count;
} Scope;

typedef struct {
    Scope scopes[SYM_MAX_SCOPES];
    int   scope_count;      /* 历史上出现过的 scope 总数（含已弹出） */
    int   stack[SYM_MAX_SCOPES];   /* 当前活跃 scope id 栈 */
    int   stack_top;
} SymTab;

void  symtab_init(SymTab *s);
int   symtab_enter_scope(SymTab *s);      /* 返回新 scope id */
void  symtab_exit_scope(SymTab *s);
int   symtab_current_scope(const SymTab *s);
bool  symtab_insert(SymTab *s, const Symbol *sym);
Symbol *symtab_lookup(SymTab *s, const char *name);
Symbol *symtab_lookup_local(SymTab *s, const char *name);

void  symtab_to_json(const SymTab *s, FILE *out);
void  symtab_print(const SymTab *s, FILE *out);

#endif
