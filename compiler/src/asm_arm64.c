#include "asm_arm64.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 辅助：判断字面量 / 临时名 / 全局名 ────────────────── */

static bool is_int_literal(const char *s) {
    if (!s || !*s) return false;
    int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (!s[i]) return false;
    for (; s[i]; i++) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static bool is_float_literal(const char *s) {
    if (!s || !*s) return false;
    bool dot = false, dig = false;
    int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    for (; s[i]; i++) {
        if (s[i] == '.') { if (dot) return false; dot = true; }
        else if (isdigit((unsigned char)s[i])) dig = true;
        else return false;
    }
    return dot && dig;
}

static long literal_value(const char *s) {
    if (is_int_literal(s)) return atol(s);
    if (is_float_literal(s)) return (long)atof(s);   /* 浮点字面量截断为整数 */
    return 0;
}

/* ── 加载/存储辅助 ───────────────────────────────────── */

/* 把 src（变量名 / 字面量）加载到 xN 寄存器。 */
static void load_into(FILE *out, const MemMap *m, const FuncFrame *f,
                      const char *src, int reg) {
    if (is_int_literal(src) || is_float_literal(src)) {
        long v = literal_value(src);
        if (v >= 0 && v <= 65535)
            fprintf(out, "    mov x%d, #%ld\n", reg, v);
        else if (v < 0 && v >= -65536)
            fprintf(out, "    mov x%d, #%ld\n", reg, v);
        else {
            /* 大常数用 movz + movk 拼 */
            unsigned long u = (unsigned long)v;
            fprintf(out, "    mov x%d, #%lu\n", reg, u & 0xFFFFu);
            if ((u >> 16) & 0xFFFFu)
                fprintf(out, "    movk x%d, #%lu, lsl #16\n", reg, (u >> 16) & 0xFFFFu);
        }
        return;
    }
    if (f) {
        const VarSlot *s = memmap_find_slot(f, src);
        if (s) {
            fprintf(out, "    ldr x%d, [x29, #%d]\n", reg, s->offset);
            return;
        }
    }
    if (m) {
        const VarSlot *g = memmap_find_global(m, src);
        if (g) {
            fprintf(out, "    adrp x%d, %s\n", reg, g->name);
            fprintf(out, "    add x%d, x%d, :lo12:%s\n", reg, reg, g->name);
            fprintf(out, "    ldr x%d, [x%d]\n", reg, reg);
            return;
        }
    }
    fprintf(out, "    /* unknown var '%s' */\n", src);
    fprintf(out, "    mov x%d, #0\n", reg);
}

/* 把 xN 寄存器的值存到 dst 变量。 */
static void store_from(FILE *out, const MemMap *m, const FuncFrame *f,
                       const char *dst, int reg) {
    if (f) {
        const VarSlot *s = memmap_find_slot(f, dst);
        if (s) {
            fprintf(out, "    str x%d, [x29, #%d]\n", reg, s->offset);
            return;
        }
    }
    if (m) {
        const VarSlot *g = memmap_find_global(m, dst);
        if (g) {
            fprintf(out, "    adrp x9, %s\n", g->name);
            fprintf(out, "    add x9, x9, :lo12:%s\n", g->name);
            fprintf(out, "    str x%d, [x9]\n", reg);
            return;
        }
    }
    fprintf(out, "    /* unknown dst '%s' */\n", dst);
}

/* 取数组基地址到 xN：栈数组用 add x<reg>, x29, #(-base)；全局用 adrp+add */
static void array_base(FILE *out, const MemMap *m, const FuncFrame *f,
                       const char *name, int reg) {
    if (f) {
        const VarSlot *s = memmap_find_slot(f, name);
        if (s) {
            /* 数组首元素在 [x29, #offset]；offset 通常是负数 */
            if (s->offset >= 0) {
                fprintf(out, "    add x%d, x29, #%d\n", reg, s->offset);
            } else {
                /* AArch64 add 立即数是非负的，用 sub */
                fprintf(out, "    sub x%d, x29, #%d\n", reg, -s->offset);
            }
            return;
        }
    }
    if (m) {
        const VarSlot *g = memmap_find_global(m, name);
        if (g) {
            fprintf(out, "    adrp x%d, %s\n", reg, g->name);
            fprintf(out, "    add x%d, x%d, :lo12:%s\n", reg, reg, g->name);
            return;
        }
    }
    fprintf(out, "    /* unknown array '%s' */\n", name);
    fprintf(out, "    mov x%d, #0\n", reg);
}

/* ── 序言 / 全局段 ──────────────────────────────────── */

static void emit_prologue(FILE *out, const MemMap *m) {
    fputs(
        "    .arch armv8-a\n"
        "    .text\n"
        "\n"
        "    .section .rodata\n"
        ".LC_print: .asciz \"%ld\\n\"\n"
        ".LC_input: .asciz \"%ld\"\n"
        "\n",
        out);

    if (m->global_count > 0) {
        fputs("    .bss\n", out);
        for (int i = 0; i < m->global_count; i++) {
            const VarSlot *g = &m->globals[i];
            fprintf(out, "    .align 3\n    .global %s\n%s:\n    .zero %d\n",
                    g->name, g->name, g->size);
        }
        fputc('\n', out);
    }

    fputs(
        "    .text\n"
        "\n"
        "    .type __print, %function\n"
        "__print:\n"
        "    stp x29, x30, [sp, #-16]!\n"
        "    mov x29, sp\n"
        "    mov x1, x0\n"
        "    adrp x0, .LC_print\n"
        "    add x0, x0, :lo12:.LC_print\n"
        "    bl printf\n"
        "    ldp x29, x30, [sp], #16\n"
        "    ret\n"
        "\n"
        "    .type __input, %function\n"
        "__input:\n"
        "    stp x29, x30, [sp, #-32]!\n"
        "    mov x29, sp\n"
        "    add x1, sp, #16\n"
        "    adrp x0, .LC_input\n"
        "    add x0, x0, :lo12:.LC_input\n"
        "    bl __isoc99_scanf\n"
        "    ldr x0, [sp, #16]\n"
        "    ldp x29, x30, [sp], #32\n"
        "    ret\n"
        "\n",
        out);
}

/* ── 函数体翻译 ──────────────────────────────────────── */

static QuadOp arith_of(QuadOp op) { return op; }
static const char *rel_cond(QuadOp op) {
    switch (op) {
        case OP_LT: return "lt";
        case OP_LE: return "le";
        case OP_EQ: return "eq";
        case OP_GT: return "gt";
        case OP_GE: return "ge";
        case OP_NE: return "ne";
        default:    return "eq";
    }
}

/* 把 quads[from..to) 翻译成一个函数体。f 是该函数的栈帧。 */
static void emit_function(FILE *out, const MemMap *m, const FuncFrame *f,
                          const QuadList *q, int from, int to) {
    const char *fname = f->name;

    /* PARAM 缓冲（CALL 时按顺序消费） */
    char param_buf[8][48];
    int  param_n = 0;

    fprintf(out, "    .type %s, %%function\n", fname);
    fprintf(out, "    .global %s\n", fname);
    fprintf(out, "%s:\n", fname);
    fprintf(out, "    stp x29, x30, [sp, #-%d]!\n", f->frame_size);
    fputs(  "    mov x29, sp\n", out);

    /* 形参从寄存器 x0..x7 spill 到栈 */
    int p_idx = 0;
    for (int i = 0; i < f->slot_count && p_idx < 8; i++) {
        if (f->slots[i].kind != SLOT_PARAM) continue;
        fprintf(out, "    str x%d, [x29, #%d]\n", p_idx, f->slots[i].offset);
        p_idx++;
    }

    for (int i = from; i < to; i++) {
        const Quad *qd = &q->items[i];
        const char *a1 = qd->arg1;
        const char *a2 = qd->arg2;
        const char *r  = qd->result;
        fprintf(out, "    /* %d: (%s, %s, %s, %s) */\n", i + 1,
                quad_op_name(qd->op), a1, a2, r);

        switch (qd->op) {
            case OP_ASSIGN:
                load_into(out, m, f, a1, 0);
                store_from(out, m, f, r, 0);
                break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: {
                load_into(out, m, f, a1, 0);
                load_into(out, m, f, a2, 1);
                const char *mn = "add";
                switch (arith_of(qd->op)) {
                    case OP_ADD: mn = "add"; break;
                    case OP_SUB: mn = "sub"; break;
                    case OP_MUL: mn = "mul"; break;
                    case OP_DIV: mn = "sdiv"; break;
                    default: break;
                }
                fprintf(out, "    %s x0, x0, x1\n", mn);
                store_from(out, m, f, r, 0);
                break;
            }
            case OP_LT: case OP_LE: case OP_EQ:
            case OP_GT: case OP_GE: case OP_NE: {
                load_into(out, m, f, a1, 0);
                load_into(out, m, f, a2, 1);
                fputs("    cmp x0, x1\n", out);
                fprintf(out, "    cset x0, %s\n", rel_cond(qd->op));
                store_from(out, m, f, r, 0);
                break;
            }
            case OP_LABEL:
                fprintf(out, "%s_%s:\n", fname, r);
                break;
            case OP_GOTO:
                fprintf(out, "    b %s_%s\n", fname, r);
                break;
            case OP_IF_FALSE:
                load_into(out, m, f, a1, 0);
                fprintf(out, "    cbz x0, %s_%s\n", fname, r);
                break;
            case OP_PARAM:
                if (param_n < 8) {
                    strncpy(param_buf[param_n], a1, 47);
                    param_buf[param_n][47] = '\0';
                    param_n++;
                }
                break;
            case OP_CALL: {
                /* a1 = callee, a2 = n, r = 结果寄存器（可能是 _）*/
                int n = atoi(a2);
                if (n > 8) n = 8;
                for (int k = 0; k < n && k < param_n; k++) {
                    load_into(out, m, f, param_buf[k], k);
                }
                fprintf(out, "    bl %s\n", a1);
                if (r && strcmp(r, "_") != 0) {
                    store_from(out, m, f, r, 0);
                }
                param_n = 0;
                break;
            }
            case OP_RETURN:
                if (a1 && strcmp(a1, "_") != 0) {
                    load_into(out, m, f, a1, 0);
                } else {
                    fputs("    mov x0, #0\n", out);
                }
                fprintf(out, "    b %s_ret\n", fname);
                break;
            case OP_IDX_R:
                array_base(out, m, f, a1, 2);
                load_into(out, m, f, a2, 1);
                fputs("    lsl x1, x1, #3\n", out);
                fputs("    ldr x0, [x2, x1]\n", out);
                store_from(out, m, f, r, 0);
                break;
            case OP_IDX_W:
                array_base(out, m, f, r, 2);
                load_into(out, m, f, a1, 0);
                load_into(out, m, f, a2, 1);
                fputs("    lsl x1, x1, #3\n", out);
                fputs("    str x0, [x2, x1]\n", out);
                break;
            case OP_PRINT:
                load_into(out, m, f, a1, 0);
                fputs("    bl __print\n", out);
                break;
            case OP_INPUT:
                fputs("    bl __input\n", out);
                store_from(out, m, f, r, 0);
                break;
            case OP_FUNC_BEGIN:
            case OP_FUNC_END:
                /* 由外层处理，不在 body 区间出现 */
                break;
            case OP_NOP:
                break;
        }
    }

    /* epilogue */
    fprintf(out, "%s_ret:\n", fname);
    fprintf(out, "    ldp x29, x30, [sp], #%d\n", f->frame_size);
    fputs(  "    ret\n\n", out);
}

/* ── 顶层入口处理 ─────────────────────────────────────── */

/* 若 quads 中存在用户的 main 函数，glibc 会自动调用它，不需要额外 entry。
 * 若用户入口函数不是 main（用顶层 CALL X 启动），生成 main: bl X; mov w0,#0; ret。 */
static void emit_entry_if_needed(FILE *out, const QuadList *q) {
    /* 找 FUNC_BEGIN 列表 */
    bool has_user_main = false;
    char top_call[48] = "";
    for (int i = 0; i < q->count; i++) {
        if (q->items[i].op == OP_FUNC_BEGIN && strcmp(q->items[i].arg1, "main") == 0)
            has_user_main = true;
    }
    if (has_user_main) return;

    /* 找文件末尾的顶层 CALL（不在任何 FUNC_BEGIN..FUNC_END 区间内） */
    int depth = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->items[i].op == OP_FUNC_BEGIN) depth++;
        else if (q->items[i].op == OP_FUNC_END) depth--;
        else if (q->items[i].op == OP_CALL && depth == 0) {
            strncpy(top_call, q->items[i].arg1, 47);
            top_call[47] = '\0';
        }
    }
    if (!top_call[0]) return;

    fprintf(out,
        "    .type main, %%function\n"
        "    .global main\n"
        "main:\n"
        "    stp x29, x30, [sp, #-16]!\n"
        "    mov x29, sp\n"
        "    bl %s\n"
        "    mov w0, #0\n"
        "    ldp x29, x30, [sp], #16\n"
        "    ret\n\n",
        top_call);
}

void asm_arm64_emit(const QuadList *q, const MemMap *m, FILE *out) {
    emit_prologue(out, m);

    /* 按 FUNC_BEGIN..FUNC_END 区间翻译每个函数 */
    for (int i = 0; i < q->count; i++) {
        if (q->items[i].op != OP_FUNC_BEGIN) continue;
        const char *fname = q->items[i].arg1;
        const FuncFrame *f = memmap_find_func(m, fname);
        if (!f) continue;
        int end = i + 1;
        while (end < q->count && q->items[end].op != OP_FUNC_END) end++;
        emit_function(out, m, f, q, i + 1, end);
        i = end;
    }

    emit_entry_if_needed(out, q);
}
