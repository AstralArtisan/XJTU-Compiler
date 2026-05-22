#include "symtab.h"

#include <stdio.h>
#include <string.h>

void symtab_init(SymTab *s) {
    memset(s, 0, sizeof(*s));
    /* 隐式开启 scope 0 = 全局作用域 */
    s->scopes[0].level   = 0;
    s->scopes[0].parent  = -1;
    s->scope_count       = 1;
    s->stack[0]          = 0;
    s->stack_top         = 0;
}

int symtab_enter_scope(SymTab *s) {
    if (s->scope_count >= SYM_MAX_SCOPES) {
        fprintf(stderr, "symtab: scope overflow\n");
        return -1;
    }
    int id = s->scope_count++;
    s->scopes[id].level  = s->stack_top + 1;
    s->scopes[id].parent = s->stack[s->stack_top];
    s->scopes[id].symbol_count = 0;
    s->stack_top++;
    s->stack[s->stack_top] = id;
    return id;
}

void symtab_exit_scope(SymTab *s) {
    if (s->stack_top > 0) s->stack_top--;
}

int symtab_current_scope(const SymTab *s) {
    return s->stack[s->stack_top];
}

bool symtab_insert(SymTab *s, const Symbol *sym) {
    int cur = s->stack[s->stack_top];
    Scope *sc = &s->scopes[cur];
    for (int i = 0; i < sc->symbol_count; i++) {
        if (strcmp(sc->symbols[i].name, sym->name) == 0) return false;
    }
    if (sc->symbol_count >= SYM_MAX_SYMBOLS) {
        fprintf(stderr, "symtab: symbol overflow in scope %d\n", cur);
        return false;
    }
    sc->symbols[sc->symbol_count]              = *sym;
    sc->symbols[sc->symbol_count].scope_level  = sc->level;
    sc->symbol_count++;
    return true;
}

Symbol *symtab_lookup_local(SymTab *s, const char *name) {
    int cur = s->stack[s->stack_top];
    Scope *sc = &s->scopes[cur];
    for (int i = 0; i < sc->symbol_count; i++) {
        if (strcmp(sc->symbols[i].name, name) == 0) return &sc->symbols[i];
    }
    return NULL;
}

Symbol *symtab_lookup(SymTab *s, const char *name) {
    /* 沿着活跃 scope 栈向外查 */
    for (int i = s->stack_top; i >= 0; i--) {
        Scope *sc = &s->scopes[s->stack[i]];
        for (int j = 0; j < sc->symbol_count; j++) {
            if (strcmp(sc->symbols[j].name, name) == 0) return &sc->symbols[j];
        }
    }
    return NULL;
}

/* ── 输出 ────────────────────────────────────────────── */

static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            default:   fputc(*s, out);     break;
        }
    }
    fputc('"', out);
}

void symtab_to_json(const SymTab *s, FILE *out) {
    fputc('[', out);
    for (int sc = 0; sc < s->scope_count; sc++) {
        if (sc) fputc(',', out);
        const Scope *cur = &s->scopes[sc];
        fprintf(out, "{\"scope\":%d,\"level\":%d,\"parent\":%d,\"symbols\":[",
                sc, cur->level, cur->parent);
        for (int i = 0; i < cur->symbol_count; i++) {
            if (i) fputc(',', out);
            const Symbol *sym = &cur->symbols[i];
            fputs("{\"name\":", out);
            json_string(out, sym->name);
            fprintf(out, ",\"type\":");
            json_string(out, base_type_name(sym->type));
            fprintf(out, ",\"is_array\":%s,\"is_func\":%s",
                    sym->is_array ? "true" : "false",
                    sym->is_func  ? "true" : "false");
            fprintf(out, ",\"line\":%d,\"col\":%d", sym->def_line, sym->def_col);
            if (sym->is_func) {
                fputs(",\"params\":[", out);
                for (int p = 0; p < sym->param_count; p++) {
                    if (p) fputc(',', out);
                    fprintf(out, "{\"type\":");
                    json_string(out, base_type_name(sym->param_types[p]));
                    fprintf(out, ",\"is_array\":%s}",
                            sym->param_is_array[p] ? "true" : "false");
                }
                fputc(']', out);
            }
            fputc('}', out);
        }
        fputs("]}", out);
    }
    fputc(']', out);
}

void symtab_print(const SymTab *s, FILE *out) {
    fprintf(out, "Symbol Table (%d scopes):\n", s->scope_count);
    for (int sc = 0; sc < s->scope_count; sc++) {
        const Scope *cur = &s->scopes[sc];
        fprintf(out, "  Scope %d (level=%d, parent=%d):\n",
                sc, cur->level, cur->parent);
        for (int i = 0; i < cur->symbol_count; i++) {
            const Symbol *sym = &cur->symbols[i];
            fprintf(out, "    %-16s %-8s%s%s @%d:%d\n",
                    sym->name, base_type_name(sym->type),
                    sym->is_array ? "[]" : "",
                    sym->is_func  ? "()" : "",
                    sym->def_line, sym->def_col);
        }
    }
}
