#ifndef DFA_H
#define DFA_H

#include <stdbool.h>
#include <stdio.h>

#define DFA_MAX_STATES   128
#define DFA_MAX_SYMBOLS  128
#define DFA_MAX_FINALS   64
#define DFA_MAX_CLASSES  32
#define DFA_MAX_KEYWORDS 32
#define DFA_LABEL_LEN    32

typedef struct {
    char name[DFA_LABEL_LEN];
    char chars[256];
    int  char_count;
} DfaCharClass;

typedef struct {
    char word[DFA_LABEL_LEN];
    char token_name[DFA_LABEL_LEN];
} DfaKeyword;

typedef struct {
    /* basic five-tuple (lab1 compatible) */
    char alphabet[DFA_MAX_SYMBOLS + 1];
    int  symbol_count;
    int  state_count;
    int  start_state;
    int  final_states[DFA_MAX_FINALS];
    int  final_count;
    int  transitions[DFA_MAX_STATES][DFA_MAX_SYMBOLS];

    /* accept labels: final_state -> token name (extended format) */
    char accept_labels[DFA_MAX_STATES][DFA_LABEL_LEN];

    /* character classes (extended format) */
    DfaCharClass classes[DFA_MAX_CLASSES];
    int class_count;
    int char_to_class[256]; /* maps raw char -> class index, -1 if unmapped */

    /* keywords (extended format) */
    DfaKeyword keywords[DFA_MAX_KEYWORDS];
    int keyword_count;

    bool extended; /* true if loaded from extended format */
} DFA;

/* lifecycle */
void dfa_init(DFA *dfa);

/* loading — auto-detects legacy vs extended format */
bool dfa_load(DFA *dfa, const char *filename);
bool dfa_validate(const DFA *dfa);

/* lab1 core */
void dfa_print(const DFA *dfa, FILE *out);
bool dfa_simulate(const DFA *dfa, const char *input);
void dfa_enumerate(const DFA *dfa, int max_len, FILE *out);

/* lab2 bridge: single-step transitions */
int  dfa_start(const DFA *dfa);
int  dfa_step(const DFA *dfa, int state, int ch);
bool dfa_is_accept(const DFA *dfa, int state);
const char *dfa_accept_label(const DFA *dfa, int state);
const char *dfa_find_keyword(const DFA *dfa, const char *word);

/* JSON output for frontend */
void dfa_to_json(const DFA *dfa, FILE *out);

#endif
