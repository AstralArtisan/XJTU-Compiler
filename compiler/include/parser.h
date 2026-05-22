#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "symtab.h"
#include "grammar.h"
#include "slr.h"
#include "token.h"

#include <stdbool.h>
#include <stdio.h>

#define PARSER_STACK_CAP   512
#define PARSER_TOKEN_CAP   4096

typedef struct ParseTrace ParseTrace; /* 前向声明：trace 在 parser.c 内部定义 */

typedef struct {
    AstArena       *arena;
    SymTab         *symtab;
    ErrList        *errors;
    const Grammar  *grammar;
    const SlrTable *slr;

    /* 终结符 → grammar 符号 id 的快速查找表（按 TokenKind 索引） */
    int  term_id_by_token[64];

    /* 状态栈与属性栈 */
    int      states[PARSER_STACK_CAP];
    AstNode *attrs[PARSER_STACK_CAP];
    int      sp;          /* 栈顶索引（指向下一个空位） */

    /* 当前函数上下文 */
    BaseType cur_func_ret;
    char     cur_func_name[AST_MAX_NAME];
    int      cur_func_ret_seen;   /* 至少有一条 return 时置 1 */

    /* 可选 trace */
    ParseTrace *trace;
} Parser;

void   parser_init(Parser *p, AstArena *arena, SymTab *symtab, ErrList *errors,
                   const Grammar *g, const SlrTable *slr);
AstNode *parser_run(Parser *p, const Token *tokens, int token_count);

/* trace 接口（可选） */
ParseTrace *parse_trace_new(void);
void   parse_trace_free(ParseTrace *t);
void   parse_trace_to_json(const ParseTrace *t, FILE *out);

#endif
