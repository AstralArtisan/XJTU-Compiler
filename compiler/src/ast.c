#include "ast.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── arena ───────────────────────────────────────────── */

void arena_init(AstArena *a) {
    memset(a, 0, sizeof(*a));
}

void arena_free(AstArena *a) {
    for (int i = 0; i < a->count; i++) free(a->nodes[i]);
    a->count = 0;
}

AstNode *ast_new(AstArena *a, AstKind kind, int line, int col) {
    if (a->count >= AST_ARENA_CAPACITY) {
        fprintf(stderr, "ast: arena overflow\n");
        return NULL;
    }
    AstNode *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->kind = kind;
    n->type = TYPE_VOID;
    n->line = line;
    n->col  = col;
    n->id   = a->count;
    a->nodes[a->count++] = n;
    return n;
}

void ast_add_child(AstNode *parent, AstNode *child) {
    if (!parent || !child) return;
    if (parent->child_count >= AST_MAX_CHILDREN) {
        fprintf(stderr, "ast: too many children on node %d\n", parent->id);
        return;
    }
    parent->children[parent->child_count++] = child;
}

/* ── 名称表 ──────────────────────────────────────────── */

const char *ast_kind_name(AstKind k) {
    switch (k) {
        case AST_PROGRAM:   return "PROGRAM";
        case AST_FUNC_DECL: return "FUNC_DECL";
        case AST_VAR_DECL:  return "VAR_DECL";
        case AST_PARAM:     return "PARAM";
        case AST_BLOCK:     return "BLOCK";
        case AST_ASSIGN:    return "ASSIGN";
        case AST_IF:        return "IF";
        case AST_WHILE:     return "WHILE";
        case AST_RETURN:    return "RETURN";
        case AST_CALL:      return "CALL";
        case AST_BIN_OP:    return "BIN_OP";
        case AST_REL_OP:    return "REL_OP";
        case AST_UNARY_OP:  return "UNARY_OP";
        case AST_INDEX:     return "INDEX";
        case AST_LIT_INT:   return "LIT_INT";
        case AST_LIT_FLOAT: return "LIT_FLOAT";
        case AST_REF:       return "REF";
        case AST_TYPE:      return "TYPE";
        case AST_PRINT:     return "PRINT";
        case AST_INPUT:     return "INPUT";
        case AST_LIST:      return "LIST";
        case AST_ERROR:     return "ERROR";
    }
    return "?";
}

const char *base_type_name(BaseType t) {
    switch (t) {
        case TYPE_VOID:  return "void";
        case TYPE_INT:   return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_BOOL:  return "bool";
        case TYPE_ERROR: return "<error>";
    }
    return "?";
}

/* ── JSON 序列化 ─────────────────────────────────────── */

static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:   fputc(*s, out);     break;
        }
    }
    fputc('"', out);
}

static void ast_json_rec(const AstNode *n, FILE *out) {
    if (!n) { fputs("null", out); return; }
    fprintf(out, "{\"id\":%d,\"kind\":", n->id);
    json_string(out, ast_kind_name(n->kind));
    fprintf(out, ",\"line\":%d,\"col\":%d", n->line, n->col);
    if (n->name[0]) { fprintf(out, ",\"name\":"); json_string(out, n->name); }
    fprintf(out, ",\"type\":");
    json_string(out, base_type_name(n->type));
    if (n->is_array)   fprintf(out, ",\"is_array\":true");
    if (n->array_size) fprintf(out, ",\"array_size\":%d", n->array_size);

    switch (n->kind) {
        case AST_LIT_INT:   fprintf(out, ",\"value\":%d", n->int_val); break;
        case AST_LIT_FLOAT: fprintf(out, ",\"value\":%g", n->float_val); break;
        default: break;
    }

    fprintf(out, ",\"children\":[");
    for (int i = 0; i < n->child_count; i++) {
        if (i) fputc(',', out);
        ast_json_rec(n->children[i], out);
    }
    fputs("]}", out);
}

void ast_to_json(const AstNode *root, FILE *out) {
    ast_json_rec(root, out);
}

/* ── 错误列表 ────────────────────────────────────────── */

void err_list_init(ErrList *e) { e->count = 0; }

void err_list_push(ErrList *e, ErrorKind kind, int line, int col, const char *fmt, ...) {
    if (e->count >= ERR_LIST_CAPACITY) return;
    CompilerError *err = &e->items[e->count++];
    err->kind = kind;
    err->line = line;
    err->col  = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
}

const char *err_kind_name(ErrorKind k) {
    switch (k) {
        case ERR_LEX: return "LEX";
        case ERR_SYN: return "SYN";
        case ERR_SEM: return "SEM";
    }
    return "?";
}

void err_list_to_json(const ErrList *e, FILE *out) {
    fputc('[', out);
    for (int i = 0; i < e->count; i++) {
        if (i) fputc(',', out);
        const CompilerError *er = &e->items[i];
        fprintf(out, "{\"line\":%d,\"col\":%d,\"kind\":", er->line, er->col);
        json_string(out, err_kind_name(er->kind));
        fprintf(out, ",\"message\":");
        json_string(out, er->message);
        fputc('}', out);
    }
    fputc(']', out);
}

void err_list_print(const ErrList *e, FILE *out) {
    if (e->count == 0) {
        fprintf(out, "No errors.\n");
        return;
    }
    fprintf(out, "%d error(s):\n", e->count);
    for (int i = 0; i < e->count; i++) {
        const CompilerError *er = &e->items[i];
        fprintf(out, "  [%s] %d:%d %s\n",
                err_kind_name(er->kind), er->line, er->col, er->message);
    }
}
