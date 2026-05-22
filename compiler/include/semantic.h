#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "symtab.h"

/* 对完整 AST 跑一遍语义分析：维护栈式作用域、检查重复/未声明、
 * 检查类型一致与函数调用签名、记录所有错误到 errs。 */
void semantic_check(AstNode *root, SymTab *st, ErrList *errs);

#endif
