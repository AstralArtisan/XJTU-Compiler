#ifndef MEMMAP_H
#define MEMMAP_H

#include "ir.h"
#include "symtab.h"

#include <stdbool.h>
#include <stdio.h>

#define MM_NAME_LEN   32
#define MM_MAX_SLOTS  64
#define MM_MAX_FUNCS  16
#define MM_MAX_GLOBALS 32

typedef enum {
    SLOT_PARAM,        /* 形参（在栈上，从寄存器 spill 而来） */
    SLOT_LOCAL,        /* 局部变量 */
    SLOT_TEMP,         /* 编译器临时变量 t1, t2, ... */
    SLOT_GLOBAL        /* 全局变量（标量或数组） */
} SlotKind;

typedef struct {
    char     name[MM_NAME_LEN];
    SlotKind kind;
    int      offset;       /* 栈：负数表示 [x29, -N]；全局变量 offset=0，用 name */
    int      size;         /* 字节数 */
    int      array_len;    /* >0 时为数组，size = array_len * 8 */
    BaseType type;
} VarSlot;

typedef struct {
    char     name[MM_NAME_LEN];
    int      frame_size;      /* 含 16 字节 x29/x30 保护，对齐到 16 */
    int      slot_count;
    VarSlot  slots[MM_MAX_SLOTS];
} FuncFrame;

typedef struct {
    FuncFrame funcs[MM_MAX_FUNCS];
    int       func_count;
    VarSlot   globals[MM_MAX_GLOBALS];
    int       global_count;
} MemMap;

void memmap_init(MemMap *m);
void memmap_build(MemMap *m, const QuadList *q, SymTab *st);

const FuncFrame *memmap_find_func(const MemMap *m, const char *name);
const VarSlot   *memmap_find_slot(const FuncFrame *f, const char *name);
const VarSlot   *memmap_find_global(const MemMap *m, const char *name);

/* 把变量名翻译成 AArch64 寻址表达式。函数级 var 用 [x29, #N]；
 * 全局用 [<name>]（asm 端再展开 adrp+ldr）。字面量原样返回。
 * 输出写入 out（≥48 字节），返回 out 指针。 */
char *memmap_addr_for(const MemMap *m, const FuncFrame *f,
                      const char *var, char out[48]);

void memmap_print(const MemMap *m, FILE *out);
void memmap_to_json(const MemMap *m, FILE *out);

#endif
