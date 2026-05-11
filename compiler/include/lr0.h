#ifndef LR0_H
#define LR0_H

#include "grammar.h"

#include <stdbool.h>
#include <stdio.h>

#define LR0_MAX_ITEMS  256
#define LR0_MAX_STATES 128
#define LR0_MAX_GOTO   (LR0_MAX_STATES * GR_MAX_SYMBOLS)
#define LR0_MAX_CONFLICT_ITEMS 8

typedef struct {
    int prod;
    int dot;
} Lr0Item;

typedef struct {
    Lr0Item items[LR0_MAX_ITEMS];
    int     item_count;
} Lr0ItemSet;

typedef struct {
    int from;
    int on_symbol;
    int to;
} Lr0Edge;

typedef enum {
    CONF_NONE,
    CONF_SHIFT_REDUCE,
    CONF_REDUCE_REDUCE
} Lr0ConflictKind;

typedef struct {
    int             state;
    Lr0ConflictKind kind;
    int             items[LR0_MAX_CONFLICT_ITEMS];
    int             item_count;
    int             on_symbol; /* 仅 shift-reduce 有意义，记移进的终结符 */
} Lr0Conflict;

typedef struct {
    const Grammar *g;
    Lr0ItemSet  states[LR0_MAX_STATES];
    int         state_count;
    Lr0Edge     edges[LR0_MAX_GOTO];
    int         edge_count;
    Lr0Conflict conflicts[LR0_MAX_STATES];
    int         conflict_count;
    bool        is_lr0;
} Lr0Collection;

void lr0_init(Lr0Collection *c, const Grammar *g);
void lr0_closure(const Grammar *g, Lr0ItemSet *I);
void lr0_goto(const Grammar *g, const Lr0ItemSet *I, int sym, Lr0ItemSet *out);
bool lr0_build(Lr0Collection *c);
void lr0_detect_conflicts(Lr0Collection *c);

void lr0_print(const Lr0Collection *c, FILE *out);
void lr0_print_set(const Grammar *g, const Lr0ItemSet *I, FILE *out);
void lr0_print_conflicts(const Lr0Collection *c, FILE *out);
void lr0_to_json(const Lr0Collection *c, FILE *out);

#endif
