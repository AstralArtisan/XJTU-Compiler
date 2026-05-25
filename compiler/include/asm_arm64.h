#ifndef ASM_ARM64_H
#define ASM_ARM64_H

#include "ir.h"
#include "memmap.h"

#include <stdio.h>

/* 把四元式 + 内存映射翻译成可被 gcc 编译的 AArch64 (GAS 语法) 汇编文本，
 * 输出到 out。生成的汇编已经包含 .data 段（PRINT/INPUT 用的格式串）、
 * __print/__input 包装函数（调 libc printf/scanf）与 user 函数。
 * 顶层 main 函数会被声明为 .global main 作为 glibc 启动入口。 */
void asm_arm64_emit(const QuadList *q, const MemMap *m, FILE *out);

#endif
