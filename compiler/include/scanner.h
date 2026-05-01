#ifndef SCANNER_H
#define SCANNER_H

#include "token.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
    int error_count;
} Scanner;

void scanner_init(Scanner *s, const char *src);
bool scanner_next(Scanner *s, Token *out);

#endif
