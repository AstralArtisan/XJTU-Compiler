#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stdio.h>

#define AST_MAX_CHILDREN   16
#define AST_MAX_NAME       32
#define AST_ARENA_CAPACITY 4096
#define AST_LEX_LIMIT      48

typedef enum {
    AST_PROGRAM,
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_PARAM,
    AST_BLOCK,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_RETURN,
    AST_CALL,
    AST_BIN_OP,
    AST_REL_OP,
    AST_UNARY_OP,
    AST_INDEX,
    AST_LIT_INT,
    AST_LIT_FLOAT,
    AST_REF,
    AST_TYPE,     /* 叶子节点：携带 BaseType 的类型标记 */
    AST_PRINT,
    AST_INPUT,
    AST_LIST,    /* 通用列表容器（Decls/StmtList/ParamList/ArgList 临时累加） */
    AST_ERROR
} AstKind;

typedef enum {
    TYPE_VOID,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,    /* 比较表达式结果 */
    TYPE_ERROR    /* 用于推断失败的占位 */
} BaseType;

typedef struct AstNode {
    AstKind   kind;
    BaseType  type;         /* 表达式与变量节点的类型；其它节点为 TYPE_VOID */
    bool      is_array;     /* 变量/参数是否为数组 */
    int       array_size;   /* 数组长度，无则为 0 */
    int       line, col;
    char      name[AST_MAX_NAME];     /* 标识符名、字面量、运算符等 */
    char      lex[AST_LEX_LIMIT];     /* 原始 lexeme（数字字面量用） */
    int       int_val;
    double    float_val;
    int       id;                       /* 节点在 arena 中的唯一编号 */
    struct AstNode *children[AST_MAX_CHILDREN];
    int       child_count;
} AstNode;

typedef struct {
    AstNode *nodes[AST_ARENA_CAPACITY];
    int      count;
} AstArena;

typedef enum {
    ERR_LEX,
    ERR_SYN,
    ERR_SEM
} ErrorKind;

typedef struct {
    int       line, col;
    ErrorKind kind;
    char      message[256];
} CompilerError;

#define ERR_LIST_CAPACITY 128

typedef struct {
    CompilerError items[ERR_LIST_CAPACITY];
    int           count;
} ErrList;

void  arena_init(AstArena *a);
void  arena_free(AstArena *a);
AstNode *ast_new(AstArena *a, AstKind kind, int line, int col);
void  ast_add_child(AstNode *parent, AstNode *child);
const char *ast_kind_name(AstKind k);
const char *base_type_name(BaseType t);
void  ast_to_json(const AstNode *root, FILE *out);

void  err_list_init(ErrList *e);
void  err_list_push(ErrList *e, ErrorKind kind, int line, int col, const char *fmt, ...);
void  err_list_to_json(const ErrList *e, FILE *out);
void  err_list_print(const ErrList *e, FILE *out);
const char *err_kind_name(ErrorKind k);

#endif
