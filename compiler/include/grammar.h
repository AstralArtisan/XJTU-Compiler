#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <stdbool.h>
#include <stdio.h>

#define GR_MAX_SYMBOLS     64
#define GR_MAX_PRODUCTIONS 128
#define GR_MAX_RHS         8
#define GR_LABEL_LEN       24

typedef enum {
    SYM_TERM,
    SYM_NONTERM,
    SYM_EPS,
    SYM_END
} SymKind;

typedef struct {
    char    name[GR_LABEL_LEN];
    SymKind kind;
} GrSymbol;

typedef struct {
    int lhs;
    int rhs[GR_MAX_RHS];
    int rhs_len;
} GrProduction;

typedef struct {
    GrSymbol     symbols[GR_MAX_SYMBOLS];
    int          symbol_count;
    GrProduction productions[GR_MAX_PRODUCTIONS];
    int          production_count;
    int          start_symbol;
    int          augmented_start;
    int          end_symbol;
    int          eps_symbol;
} Grammar;

void grammar_init(Grammar *g);
bool grammar_load(Grammar *g, const char *filename);
void grammar_augment(Grammar *g);
int  grammar_intern(Grammar *g, const char *name, SymKind kind);
int  grammar_lookup(const Grammar *g, const char *name);
void grammar_print(const Grammar *g, FILE *out);

const char *grammar_symbol_name(const Grammar *g, int sym);
SymKind     grammar_symbol_kind(const Grammar *g, int sym);

#endif
