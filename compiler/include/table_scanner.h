#ifndef TABLE_SCANNER_H
#define TABLE_SCANNER_H

#include "dfa.h"
#include "token.h"
#include <stdbool.h>

typedef struct {
    const DFA *dfa;
    const char *src;
    size_t pos;
    int line;
    int col;
    int error_count;
} TableScanner;

void ts_init(TableScanner *ts, const DFA *dfa, const char *src);
bool ts_next(TableScanner *ts, Token *out);

#endif
