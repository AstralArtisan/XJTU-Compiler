#ifndef SLR_H
#define SLR_H

#include "grammar.h"
#include "lr0.h"

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    SLR_ACTION_ERROR = 0,
    SLR_ACTION_SHIFT,
    SLR_ACTION_REDUCE,
    SLR_ACTION_ACCEPT
} SlrActionKind;

typedef struct {
    SlrActionKind kind;
    int           target; /* shift: 目标状态; reduce: 产生式编号; 其它: -1 */
} SlrAction;

/* FIRST / FOLLOW 集合，按非终结符 ID 索引；终结符位置留空。 */
typedef struct {
    bool first[GR_MAX_SYMBOLS];   /* 终结符位为 true 表示属于该 NT 的 FIRST */
    bool follow[GR_MAX_SYMBOLS];  /* 终结符位为 true 表示属于该 NT 的 FOLLOW */
    bool nullable;                /* NT 可推出 ε 时为 true */
} SlrSets;

typedef struct {
    int           state;
    int           on_symbol;          /* 冲突所在的终结符 */
    SlrActionKind existing_kind;      /* 已填入的动作 */
    int           existing_target;
    SlrActionKind incoming_kind;      /* 尝试填入但被拒的动作 */
    int           incoming_target;
} SlrConflict;

#define SLR_MAX_CONFLICTS 256

typedef struct {
    const Grammar      *g;
    const Lr0Collection *lr0;

    SlrSets sets[GR_MAX_SYMBOLS];                     /* 按非终结符 ID 索引 */
    SlrAction action[LR0_MAX_STATES][GR_MAX_SYMBOLS]; /* 终结符列 + $ */
    int       goto_tab[LR0_MAX_STATES][GR_MAX_SYMBOLS]; /* 非终结符列；-1=空 */

    SlrConflict conflicts[SLR_MAX_CONFLICTS];
    int         conflict_count;
    bool        is_slr1;
} SlrTable;

void slr_init(SlrTable *t, const Grammar *g, const Lr0Collection *lr0);
void slr_compute_first(const Grammar *g, SlrSets sets[]);
void slr_compute_follow(const Grammar *g, SlrSets sets[]);
bool slr_build(SlrTable *t);

void slr_print(const SlrTable *t, FILE *out);
void slr_print_sets(const SlrTable *t, FILE *out);
void slr_print_tables(const SlrTable *t, FILE *out);
void slr_print_conflicts(const SlrTable *t, FILE *out);
void slr_to_json(const SlrTable *t, FILE *out);

#endif
