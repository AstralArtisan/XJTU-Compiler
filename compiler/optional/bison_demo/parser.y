/* parser.y — bison 文法（与 expr.grammar 完全等价的 SLR/LALR 分层版本）
 * 与手写 SLR(1) 实现做对照。运行 `make` 后可观察 parser.output 中 bison 自动生成
 * 的 ACTION/GOTO 表和状态机，与 compiler 子命令 `slr data/expr.grammar` 输出对比。
 */
%{
#include <stdio.h>
#include <stdlib.h>

int yylex(void);
void yyerror(const char *s);

static int g_accepted = 0;
%}

%define parse.error verbose
%token ADD MUL LPAR RPAR ID

%%
S : E              { g_accepted = 1; }
  ;

E : E ADD T
  | T
  ;

T : T MUL F
  | F
  ;

F : LPAR E RPAR
  | ID
  ;
%%

void yyerror(const char *s) {
    fprintf(stderr, "parser: %s\n", s);
}

int main(void) {
    int rc = yyparse();
    if (rc == 0 && g_accepted) {
        printf("ACCEPT\n");
        return 0;
    }
    printf("REJECT\n");
    return 1;
}
