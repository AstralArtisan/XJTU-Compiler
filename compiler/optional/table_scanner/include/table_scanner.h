#ifndef TABLE_SCANNER_H
#define TABLE_SCANNER_H

#include <stdbool.h>
#include <stddef.h>

#define TS_LEXEME_LIMIT 128
#define TS_NAME_LIMIT 32
#define TS_MAX_STATES 64
#define TS_MAX_CLASSES 32
#define TS_MAX_KEYWORDS 32

typedef struct {
    char name[TS_NAME_LIMIT];
} TsName;

typedef struct {
    char ch;             /* literal character; '\0' means class identifier match */
    char class_name[TS_NAME_LIMIT];
} TsCharClass;

typedef struct {
    char from[TS_NAME_LIMIT];
    char input[TS_NAME_LIMIT];
    char to[TS_NAME_LIMIT];
} TsTransitionRaw;

typedef struct {
    /* indexes are state ids (0..n_states-1), class ids (0..n_classes-1) */
    int n_states;
    int n_classes;
    char state_names[TS_MAX_STATES][TS_NAME_LIMIT];
    char class_names[TS_MAX_CLASSES][TS_NAME_LIMIT];

    /* class_of_char[ch] = class id, or -1 if char is not classified */
    int class_of_char[256];

    /* trans[s][c] = next state id, or -1 if dead */
    int trans[TS_MAX_STATES][TS_MAX_CLASSES];

    int start;
    /* token_of_state[s] = token name as string, or empty if not accepting */
    char token_of_state[TS_MAX_STATES][TS_NAME_LIMIT];

    /* keywords */
    int n_keywords;
    char kw_word[TS_MAX_KEYWORDS][TS_NAME_LIMIT];
    char kw_token[TS_MAX_KEYWORDS][TS_NAME_LIMIT];
} TsTable;

typedef struct {
    char token[TS_NAME_LIMIT];
    char lexeme[TS_LEXEME_LIMIT];
    int line;
    int col;
} TsToken;

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
    int errors;
} TsScanner;

bool ts_load(TsTable *t, const char *path, char *err, size_t err_size);
void ts_dump(const TsTable *t);

void ts_init(TsScanner *s, const char *src);
bool ts_next(const TsTable *t, TsScanner *s, TsToken *out);

#endif
