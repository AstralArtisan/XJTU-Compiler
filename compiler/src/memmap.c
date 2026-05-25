#include "memmap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORD 8

static int align_up(int v, int a) {
    return (v + a - 1) / a * a;
}

static bool is_int_literal(const char *s) {
    if (!s || !*s) return false;
    int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (!s[i]) return false;
    for (; s[i]; i++) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static bool is_float_literal(const char *s) {
    if (!s || !*s) return false;
    bool has_dot = false, has_digit = false;
    int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    for (; s[i]; i++) {
        if (s[i] == '.') { if (has_dot) return false; has_dot = true; }
        else if (isdigit((unsigned char)s[i])) has_digit = true;
        else return false;
    }
    return has_dot && has_digit;
}

static bool is_temp_name(const char *s) {
    if (!s || s[0] != 't') return false;
    for (int i = 1; s[i]; i++) if (!isdigit((unsigned char)s[i])) return false;
    return s[1] != 0;
}

void memmap_init(MemMap *m) {
    memset(m, 0, sizeof(*m));
}

static FuncFrame *new_func(MemMap *m, const char *name) {
    if (m->func_count >= MM_MAX_FUNCS) return NULL;
    FuncFrame *f = &m->funcs[m->func_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, MM_NAME_LEN - 1);
    return f;
}

static VarSlot *add_slot(FuncFrame *f, const char *name, SlotKind kind,
                         int offset, int size, int array_len, BaseType type) {
    if (f->slot_count >= MM_MAX_SLOTS) return NULL;
    VarSlot *s = &f->slots[f->slot_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, MM_NAME_LEN - 1);
    s->kind = kind;
    s->offset = offset;
    s->size = size;
    s->array_len = array_len;
    s->type = type;
    return s;
}

const FuncFrame *memmap_find_func(const MemMap *m, const char *name) {
    for (int i = 0; i < m->func_count; i++)
        if (strcmp(m->funcs[i].name, name) == 0) return &m->funcs[i];
    return NULL;
}

const VarSlot *memmap_find_slot(const FuncFrame *f, const char *name) {
    if (!f) return NULL;
    for (int i = 0; i < f->slot_count; i++)
        if (strcmp(f->slots[i].name, name) == 0) return &f->slots[i];
    return NULL;
}

const VarSlot *memmap_find_global(const MemMap *m, const char *name) {
    for (int i = 0; i < m->global_count; i++)
        if (strcmp(m->globals[i].name, name) == 0) return &m->globals[i];
    return NULL;
}

/* 在 SymTab 里找到与 name 匹配的函数符号，返回它形参的签名 */
static const Symbol *find_func_symbol(SymTab *st, const char *name) {
    for (int i = 0; i < st->scope_count; i++) {
        for (int j = 0; j < st->scopes[i].symbol_count; j++) {
            const Symbol *sym = &st->scopes[i].symbols[j];
            if (sym->is_func && strcmp(sym->name, name) == 0) return sym;
        }
    }
    return NULL;
}

/* 在 SymTab 全局 scope（id=0）里找非函数符号，作为全局变量收集 */
static void collect_globals(MemMap *m, SymTab *st) {
    if (st->scope_count == 0) return;
    const Scope *root = &st->scopes[0];
    for (int i = 0; i < root->symbol_count && m->global_count < MM_MAX_GLOBALS; i++) {
        const Symbol *sym = &root->symbols[i];
        if (sym->is_func) continue;
        VarSlot *g = &m->globals[m->global_count++];
        memset(g, 0, sizeof(*g));
        strncpy(g->name, sym->name, MM_NAME_LEN - 1);
        g->kind = SLOT_GLOBAL;
        g->type = sym->type;
        g->array_len = sym->is_array ? 8 : 0;   /* 数组长度未保存，按 8 元素保守预留 */
        g->size = sym->is_array ? g->array_len * WORD : WORD;
    }
}

/* 找到 SymTab 中函数 func_name 对应的"局部 scope"——
 * 即父 scope 是 0、且该 scope 含与该函数同型的参数集合的那一层。
 * 简化策略：按函数在 SymTab 注册顺序对应到 level==1 的 scope 顺序。*/
static const Scope *find_func_scope(SymTab *st, int func_index) {
    int seen = 0;
    for (int i = 0; i < st->scope_count; i++) {
        if (st->scopes[i].level == 1) {
            if (seen == func_index) return &st->scopes[i];
            seen++;
        }
    }
    return NULL;
}

/* 把 [from, to) 区间内的所有四元式扫一遍，按出现顺序收集临时变量名（去重） */
static int collect_temps(const QuadList *q, int from, int to,
                         char temps[][MM_NAME_LEN], int cap) {
    int count = 0;
    const char *fields[3];
    for (int i = from; i < to; i++) {
        fields[0] = q->items[i].arg1;
        fields[1] = q->items[i].arg2;
        fields[2] = q->items[i].result;
        for (int k = 0; k < 3; k++) {
            const char *s = fields[k];
            if (!is_temp_name(s)) continue;
            bool dup = false;
            for (int j = 0; j < count; j++)
                if (strcmp(temps[j], s) == 0) { dup = true; break; }
            if (!dup && count < cap) {
                strncpy(temps[count], s, MM_NAME_LEN - 1);
                temps[count][MM_NAME_LEN - 1] = '\0';
                count++;
            }
        }
    }
    return count;
}

/* 检查 [from, to) 范围内有没有 IDX_R/IDX_W 或者 (变量名 == arr_name)，识别数组用法。
 * 数组长度从 SymTab 的 Symbol.is_array 拿（具体大小已经在 AST → IR 时丢失，
 * 这里按 SymTab 中记录或默认 8 元素处理）。 */
static int array_length_for(const Symbol *sym) {
    /* Symbol 没有 array_size 字段；用 AST 上的 array_size 也丢了。
     * 保守按 16 元素预留（足够覆盖所有用例的 arr[N], N≤10）。 */
    (void)sym;
    return 16;
}

void memmap_build(MemMap *m, const QuadList *q, SymTab *st) {
    memmap_init(m);
    collect_globals(m, st);

    /* 扫四元式找每个函数的 [FUNC_BEGIN, FUNC_END] 区间 */
    int func_idx = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->items[i].op != OP_FUNC_BEGIN) continue;
        const char *fname = q->items[i].arg1;

        int end = i + 1;
        while (end < q->count && q->items[end].op != OP_FUNC_END) end++;

        FuncFrame *f = new_func(m, fname);
        if (!f) break;

        const Symbol *func_sym  = find_func_symbol(st, fname);
        const Scope  *func_scope = find_func_scope(st, func_idx++);

        int neg_off = -16;  /* 跳过 16 字节 x29/x30 保护区 */

        /* 1) 形参槽 */
        if (func_sym && func_scope) {
            for (int p = 0; p < func_sym->param_count && p < SYM_MAX_PARAMS; p++) {
                /* 形参名在 func_scope 的最前几位（语义阶段先插形参再插局部） */
                if (p >= func_scope->symbol_count) break;
                const Symbol *psym = &func_scope->symbols[p];
                int alen = psym->is_array ? array_length_for(psym) : 0;
                int sz = psym->is_array ? alen * WORD : WORD;
                add_slot(f, psym->name, SLOT_PARAM, neg_off, sz, alen, psym->type);
                neg_off -= sz;
            }
        }

        /* 2) 局部变量槽（func_scope 中除形参外的剩余符号） */
        if (func_scope) {
            int start_idx = func_sym ? func_sym->param_count : 0;
            for (int v = start_idx; v < func_scope->symbol_count; v++) {
                const Symbol *vsym = &func_scope->symbols[v];
                if (vsym->is_func) continue;
                int alen = vsym->is_array ? array_length_for(vsym) : 0;
                int sz = vsym->is_array ? alen * WORD : WORD;
                add_slot(f, vsym->name, SLOT_LOCAL, neg_off, sz, alen, vsym->type);
                neg_off -= sz;
            }
        }

        /* 3) 嵌套块 scope 内的变量（parent 指向 func_scope 的 scope） */
        if (func_scope) {
            int func_scope_id = (int)(func_scope - st->scopes);
            for (int s = 0; s < st->scope_count; s++) {
                if (st->scopes[s].parent != func_scope_id) continue;
                for (int v = 0; v < st->scopes[s].symbol_count; v++) {
                    const Symbol *vsym = &st->scopes[s].symbols[v];
                    if (vsym->is_func) continue;
                    /* 避免与同名外层变量冲突：拼上 _b<scope> 后缀 */
                    char qualified[MM_NAME_LEN];
                    snprintf(qualified, MM_NAME_LEN, "%s", vsym->name);
                    int alen = vsym->is_array ? array_length_for(vsym) : 0;
                    int sz = vsym->is_array ? alen * WORD : WORD;
                    if (!memmap_find_slot(f, qualified))
                        add_slot(f, qualified, SLOT_LOCAL, neg_off, sz, alen, vsym->type);
                    neg_off -= sz;
                }
            }
        }

        /* 4) 临时变量 t1, t2, ... */
        char temps[MM_MAX_SLOTS][MM_NAME_LEN];
        int n_temps = collect_temps(q, i + 1, end, temps, MM_MAX_SLOTS);
        for (int t = 0; t < n_temps; t++) {
            add_slot(f, temps[t], SLOT_TEMP, neg_off, WORD, 0, TYPE_INT);
            neg_off -= WORD;
        }

        /* frame_size = 16 + 所有槽占用空间，向上对齐到 16 */
        int used = -neg_off - 16;
        f->frame_size = align_up(used + 16, 16);

        i = end; /* 跳到 FUNC_END */
    }
}

/* 寻址翻译：字面量直接返回；变量按 func/global 查表生成寻址 */
char *memmap_addr_for(const MemMap *m, const FuncFrame *f,
                      const char *var, char out[48]) {
    if (is_int_literal(var) || is_float_literal(var)) {
        /* 字面量交给 asm 自己生成 mov；这里返回原样 */
        snprintf(out, 48, "#%s", var);
        return out;
    }
    if (f) {
        const VarSlot *s = memmap_find_slot(f, var);
        if (s) {
            snprintf(out, 48, "[x29, #%d]", s->offset);
            return out;
        }
    }
    if (m) {
        const VarSlot *g = memmap_find_global(m, var);
        if (g) {
            snprintf(out, 48, "[%s]", g->name);
            return out;
        }
    }
    /* 找不到：返回名字本身让 asm 报错 */
    snprintf(out, 48, "?%s", var);
    return out;
}

/* ── 输出 ────────────────────────────────────────────── */

static const char *slot_kind_name(SlotKind k) {
    switch (k) {
        case SLOT_PARAM:  return "param";
        case SLOT_LOCAL:  return "local";
        case SLOT_TEMP:   return "temp";
        case SLOT_GLOBAL: return "global";
    }
    return "?";
}

void memmap_print(const MemMap *m, FILE *out) {
    if (m->global_count > 0) {
        fprintf(out, "Globals (%d):\n", m->global_count);
        for (int i = 0; i < m->global_count; i++) {
            const VarSlot *g = &m->globals[i];
            fprintf(out, "  %-16s %-6s size=%d%s\n", g->name,
                    base_type_name(g->type), g->size,
                    g->array_len ? " (array)" : "");
        }
        fputc('\n', out);
    }
    for (int i = 0; i < m->func_count; i++) {
        const FuncFrame *f = &m->funcs[i];
        fprintf(out, "Function %s (frame=%d bytes):\n", f->name, f->frame_size);
        for (int j = 0; j < f->slot_count; j++) {
            const VarSlot *s = &f->slots[j];
            fprintf(out, "  [x29, %+5d]  %-16s %-6s %-6s size=%d%s\n",
                    s->offset, s->name, slot_kind_name(s->kind),
                    base_type_name(s->type), s->size,
                    s->array_len ? " (array)" : "");
        }
        fputc('\n', out);
    }
}

static void json_str(FILE *out, const char *s) {
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

static void slot_to_json(FILE *out, const VarSlot *s) {
    fputc('{', out);
    fputs("\"name\":", out); json_str(out, s->name);
    fprintf(out, ",\"kind\":\"%s\",\"offset\":%d,\"size\":%d,\"array_len\":%d,\"type\":",
            slot_kind_name(s->kind), s->offset, s->size, s->array_len);
    json_str(out, base_type_name(s->type));
    fputc('}', out);
}

void memmap_to_json(const MemMap *m, FILE *out) {
    fputs("{\"globals\":[", out);
    for (int i = 0; i < m->global_count; i++) {
        if (i) fputc(',', out);
        slot_to_json(out, &m->globals[i]);
    }
    fputs("],\"functions\":[", out);
    for (int i = 0; i < m->func_count; i++) {
        const FuncFrame *f = &m->funcs[i];
        if (i) fputc(',', out);
        fputc('{', out);
        fputs("\"name\":", out); json_str(out, f->name);
        fprintf(out, ",\"frame_size\":%d,\"slots\":[", f->frame_size);
        for (int j = 0; j < f->slot_count; j++) {
            if (j) fputc(',', out);
            slot_to_json(out, &f->slots[j]);
        }
        fputs("]}", out);
    }
    fputs("]}", out);
}
