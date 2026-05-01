#include "table_scanner.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void ts_init(TableScanner *ts, const DFA *dfa, const char *src) {
    ts->dfa = dfa;
    ts->src = src;
    ts->pos = 0;
    ts->line = 1;
    ts->col = 1;
    ts->error_count = 0;
}

static int ts_peek(TableScanner *ts, size_t k) {
    unsigned char c = (unsigned char)ts->src[ts->pos + k];
    return c ? c : -1;
}

static int ts_advance(TableScanner *ts) {
    int c = ts_peek(ts, 0);
    if (c < 0) return -1;
    ts->pos++;
    if (c == '\n') { ts->line++; ts->col = 1; }
    else           { ts->col++; }
    return c;
}

static void ts_skip_ws_and_comments(TableScanner *ts) {
    for (;;) {
        int c = ts_peek(ts, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ts_advance(ts);
        } else if (c == '/' && ts_peek(ts, 1) == '/') {
            while (ts_peek(ts, 0) >= 0 && ts_peek(ts, 0) != '\n')
                ts_advance(ts);
        } else if (c == '/' && ts_peek(ts, 1) == '*') {
            ts_advance(ts); ts_advance(ts);
            while (ts_peek(ts, 0) >= 0 &&
                   !(ts_peek(ts, 0) == '*' && ts_peek(ts, 1) == '/'))
                ts_advance(ts);
            if (ts_peek(ts, 0) >= 0) { ts_advance(ts); ts_advance(ts); }
        } else {
            break;
        }
    }
}

static TokenKind label_to_kind(const char *label) {
    if (!label) return TK_ERR;
    static const struct { const char *name; TokenKind kind; } MAP[] = {
        {"ID",TK_ID},{"NUM",TK_NUM},{"FLOAT",TK_FLOAT_LIT},
        {"ADD",TK_ADD},{"SUB",TK_SUB},{"MUL",TK_MUL},{"DIV",TK_DIV},
        {"LT",TK_LT},{"LE",TK_LE},{"EQ",TK_EQ},{"GT",TK_GT},{"GE",TK_GE},{"NE",TK_NE},
        {"AND",TK_AND},{"OR",TK_OR},{"NOT",TK_NOT},
        {"ASG",TK_ASG},{"AAS",TK_AAS},{"AAA",TK_AAA},
        {"LPAR",TK_LPAR},{"RPAR",TK_RPAR},{"LBK",TK_LBK},{"RBK",TK_RBK},
        {"LBR",TK_LBR},{"RBR",TK_RBR},
        {"CMA",TK_CMA},{"COL",TK_COL},{"SCO",TK_SCO},{"DOT",TK_DOT},
        {"INT",TK_INT},{"FLOAT_KW",TK_FLOAT_KW},{"VOID",TK_VOID},
        {"IF",TK_IF},{"ELSE",TK_ELSE},{"WHILE",TK_WHILE},
        {"RETURN",TK_RETURN},{"INPUT",TK_INPUT},{"PRINT",TK_PRINT},
    };
    for (size_t i = 0; i < sizeof(MAP)/sizeof(MAP[0]); i++)
        if (strcmp(label, MAP[i].name) == 0) return MAP[i].kind;
    return TK_ERR;
}

bool ts_next(TableScanner *ts, Token *out) {
    ts_skip_ws_and_comments(ts);

    int start_line = ts->line, start_col = ts->col;
    size_t start = ts->pos;

    if (ts_peek(ts, 0) < 0) {
        out->kind = TK_EOF;
        out->lexeme[0] = '\0';
        out->line = start_line;
        out->col = start_col;
        return false;
    }

    int state = dfa_start(ts->dfa);
    int last_accept_state = -1;
    size_t last_accept_pos = start;
    int last_accept_line = start_line;
    int last_accept_col = start_col;

    while (ts_peek(ts, 0) >= 0) {
        int ch = ts_peek(ts, 0);
        int next = dfa_step(ts->dfa, state, ch);
        if (next < 0) break;
        ts_advance(ts);
        state = next;
        if (dfa_is_accept(ts->dfa, state)) {
            last_accept_state = state;
            last_accept_pos = ts->pos;
            last_accept_line = ts->line;
            last_accept_col = ts->col;
        }
    }

    if (last_accept_state >= 0) {
        ts->pos = last_accept_pos;
        ts->line = last_accept_line;
        ts->col = last_accept_col;

        size_t len = last_accept_pos - start;
        if (len >= TOKEN_LEXEME_LIMIT) len = TOKEN_LEXEME_LIMIT - 1;
        memcpy(out->lexeme, ts->src + start, len);
        out->lexeme[len] = '\0';
        out->line = start_line;
        out->col = start_col;

        const char *label = dfa_accept_label(ts->dfa, last_accept_state);
        out->kind = label_to_kind(label);

        if (out->kind == TK_ID) {
            const char *kw = dfa_find_keyword(ts->dfa, out->lexeme);
            if (kw) out->kind = label_to_kind(kw);
        }
        return true;
    }

    ts_advance(ts);
    size_t len = ts->pos - start;
    if (len >= TOKEN_LEXEME_LIMIT) len = TOKEN_LEXEME_LIMIT - 1;
    memcpy(out->lexeme, ts->src + start, len);
    out->lexeme[len] = '\0';
    out->line = start_line;
    out->col = start_col;
    out->kind = TK_ERR;
    ts->error_count++;
    fprintf(stderr, "[ERROR] illegal char '%s' at %d:%d, skipped\n",
            out->lexeme, start_line, start_col);
    return ts_next(ts, out);
}
