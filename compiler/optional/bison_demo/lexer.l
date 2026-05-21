/* lexer.l — flex 词法规则
 * 与 compiler/data/expr.grammar 中的终结符对齐：
 *   ADD MUL LPAR RPAR ID
 * 编译：见 Makefile（先经 bison 生成 parser.tab.h，再编译此文件）
 */
%{
#include "parser.tab.h"
#include <stdio.h>
%}

%option noyywrap
%option nounput
%option noinput

%%
[ \t\r\n]+              { /* 跳过空白 */ }
"+"                     { return ADD; }
"*"                     { return MUL; }
"("                     { return LPAR; }
")"                     { return RPAR; }
[A-Za-z_][A-Za-z0-9_]*  { return ID; }
.                       { fprintf(stderr, "lexer: unexpected char '%c'\n", yytext[0]); return YYerror; }
%%
