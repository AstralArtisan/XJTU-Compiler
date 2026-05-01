#include "table_scanner.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_state(const TsTable *t, const char *name) {
    for (int i = 0; i < t->n_states; ++i) {
        if (strcmp(t->state_names[i], name) == 0) return i;
    }
    return -1;
}

static int find_class(const TsTable *t, const char *name) {
    for (int i = 0; i < t->n_classes; ++i) {
        if (strcmp(t->class_names[i], name) == 0) return i;
    }
    return -1;
}

static int add_state(TsTable *t, const char *name) {
    int id = find_state(t, name);
    if (id >= 0) return id;
    if (t->n_states >= TS_MAX_STATES) return -1;
    snprintf(t->state_names[t->n_states], TS_NAME_LIMIT, "%s", name);
    return t->n_states++;
}

static int add_class(TsTable *t, const char *name) {
    int id = find_class(t, name);
    if (id >= 0) return id;
    if (t->n_classes >= TS_MAX_CLASSES) return -1;
    snprintf(t->class_names[t->n_classes], TS_NAME_LIMIT, "%s", name);
    return t->n_classes++;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

bool ts_load(TsTable *t, const char *path, char *err, size_t err_size) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < TS_MAX_STATES; ++i)
        for (int j = 0; j < TS_MAX_CLASSES; ++j) t->trans[i][j] = -1;
    for (int i = 0; i < 256; ++i) t->class_of_char[i] = -1;
    t->start = -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return false;
    }
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *content = trim(line);
        if (*content == '\0') continue;

        /* directive lines */
        if (strncmp(content, "CHARCLASS", 9) == 0) {
            /* CHARCLASS <name> <chars or "LETTER"|"DIGIT"|"REST"> */
            char *p = content + 9;
            char *name = strtok(p, " \t");
            char *spec = strtok(NULL, "");
            if (!name || !spec) {
                snprintf(err, err_size, "line %d: bad CHARCLASS", lineno);
                fclose(f); return false;
            }
            spec = trim(spec);
            int cid = add_class(t, name);
            if (cid < 0) {
                snprintf(err, err_size, "line %d: too many classes", lineno);
                fclose(f); return false;
            }
            if (strcmp(spec, "LETTER") == 0) {
                for (int c = 'a'; c <= 'z'; ++c) if (c != 'e') t->class_of_char[c] = cid;
                for (int c = 'A'; c <= 'Z'; ++c) if (c != 'E') t->class_of_char[c] = cid;
                t->class_of_char['_'] = cid;
            } else if (strcmp(spec, "DIGIT") == 0) {
                for (int c = '0'; c <= '9'; ++c) t->class_of_char[c] = cid;
            } else if (strcmp(spec, "EXP") == 0) {
                t->class_of_char['e'] = cid;
                t->class_of_char['E'] = cid;
            } else {
                /* literal characters: each char in spec becomes member of this class */
                for (char *p2 = spec; *p2; ++p2) {
                    if (isspace((unsigned char)*p2)) continue;
                    t->class_of_char[(unsigned char)*p2] = cid;
                }
            }
            continue;
        }
        if (strncmp(content, "START", 5) == 0) {
            char *name = trim(content + 5);
            int sid = add_state(t, name);
            t->start = sid;
            continue;
        }
        if (strncmp(content, "TRANS", 5) == 0) {
            char *p = content + 5;
            char *from = strtok(p, " \t");
            char *cls  = strtok(NULL, " \t");
            char *to   = strtok(NULL, " \t");
            if (!from || !cls || !to) {
                snprintf(err, err_size, "line %d: bad TRANS", lineno);
                fclose(f); return false;
            }
            int fid = add_state(t, from);
            int tid = add_state(t, to);
            int cid = add_class(t, cls);
            if (fid < 0 || tid < 0 || cid < 0) {
                snprintf(err, err_size, "line %d: too many states/classes", lineno);
                fclose(f); return false;
            }
            t->trans[fid][cid] = tid;
            continue;
        }
        if (strncmp(content, "ACCEPT", 6) == 0) {
            char *p = content + 6;
            char *st = strtok(p, " \t");
            char *tok = strtok(NULL, " \t");
            if (!st || !tok) {
                snprintf(err, err_size, "line %d: bad ACCEPT", lineno);
                fclose(f); return false;
            }
            int sid = add_state(t, st);
            snprintf(t->token_of_state[sid], TS_NAME_LIMIT, "%s", tok);
            continue;
        }
        if (strncmp(content, "KEYWORD", 7) == 0) {
            char *p = content + 7;
            char *word = strtok(p, " \t");
            char *tok = strtok(NULL, " \t");
            if (!word || !tok) {
                snprintf(err, err_size, "line %d: bad KEYWORD", lineno);
                fclose(f); return false;
            }
            if (t->n_keywords >= TS_MAX_KEYWORDS) {
                snprintf(err, err_size, "line %d: too many keywords", lineno);
                fclose(f); return false;
            }
            snprintf(t->kw_word[t->n_keywords], TS_NAME_LIMIT, "%s", word);
            snprintf(t->kw_token[t->n_keywords], TS_NAME_LIMIT, "%s", tok);
            t->n_keywords++;
            continue;
        }

        snprintf(err, err_size, "line %d: unknown directive: %s", lineno, content);
        fclose(f);
        return false;
    }
    fclose(f);
    if (t->start < 0) {
        snprintf(err, err_size, "no START defined");
        return false;
    }
    return true;
}

void ts_dump(const TsTable *t) {
    printf("[table] %d states, %d classes, %d keywords\n",
           t->n_states, t->n_classes, t->n_keywords);
    printf("  start = %s\n", t->state_names[t->start]);
    int accept_count = 0;
    for (int i = 0; i < t->n_states; ++i) {
        if (t->token_of_state[i][0]) accept_count++;
    }
    printf("  accept states = %d\n", accept_count);
}

void ts_init(TsScanner *s, const char *src) {
    s->src = src;
    s->pos = 0;
    s->line = 1;
    s->col = 1;
    s->errors = 0;
}

static int peek(const TsScanner *s, size_t k) {
    unsigned char c = (unsigned char)s->src[s->pos + k];
    return c ? c : -1;
}

static void advance(TsScanner *s) {
    int c = peek(s, 0);
    if (c == -1) return;
    s->pos++;
    if (c == '\n') { s->line++; s->col = 1; }
    else { s->col++; }
}

static void skip_ws(TsScanner *s) {
    for (;;) {
        int c = peek(s, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance(s);
        else if (c == '/' && peek(s, 1) == '/') {
            while (peek(s, 0) != -1 && peek(s, 0) != '\n') advance(s);
        }
        else if (c == '/' && peek(s, 1) == '*') {
            advance(s); advance(s);
            while (peek(s, 0) != -1 && !(peek(s, 0) == '*' && peek(s, 1) == '/')) advance(s);
            if (peek(s, 0) != -1) { advance(s); advance(s); }
        }
        else break;
    }
}

bool ts_next(const TsTable *t, TsScanner *s, TsToken *out) {
restart:
    skip_ws(s);
    if (peek(s, 0) == -1) {
        out->token[0] = '\0';
        out->lexeme[0] = '\0';
        return false;
    }

    int start_line = s->line, start_col = s->col;
    size_t start_pos = s->pos;
    int state = t->start;
    int last_accept_state = -1;
    size_t last_accept_pos = 0;
    int last_accept_line = 0, last_accept_col = 0;

    if (t->token_of_state[state][0]) {
        last_accept_state = state;
        last_accept_pos = s->pos;
        last_accept_line = s->line;
        last_accept_col = s->col;
    }

    while (peek(s, 0) != -1) {
        int c = peek(s, 0);
        int cid = t->class_of_char[c];
        int nxt = (cid >= 0) ? t->trans[state][cid] : -1;
        if (nxt < 0) break;
        advance(s);
        state = nxt;
        if (t->token_of_state[state][0]) {
            last_accept_state = state;
            last_accept_pos = s->pos;
            last_accept_line = s->line;
            last_accept_col = s->col;
        }
    }

    if (last_accept_state < 0) {
        int bad = peek(s, 0);
        s->errors++;
        fprintf(stderr, "[ERROR] illegal char '%c' (0x%02x) at %d:%d, skipped\n",
                (bad >= 32 && bad < 127) ? bad : '?', bad, s->line, s->col);
        advance(s);
        goto restart;
    }

    /* rewind to last accept */
    s->pos = last_accept_pos;
    s->line = last_accept_line;
    s->col = last_accept_col;
    size_t len = s->pos - start_pos;
    if (len >= TS_LEXEME_LIMIT) len = TS_LEXEME_LIMIT - 1;
    memcpy(out->lexeme, s->src + start_pos, len);
    out->lexeme[len] = '\0';
    out->line = start_line;
    out->col = start_col;
    snprintf(out->token, TS_NAME_LIMIT, "%s", t->token_of_state[last_accept_state]);

    /* keyword promotion */
    if (strcmp(out->token, "ID") == 0) {
        for (int i = 0; i < t->n_keywords; ++i) {
            if (strcmp(out->lexeme, t->kw_word[i]) == 0) {
                snprintf(out->token, TS_NAME_LIMIT, "%s", t->kw_token[i]);
                break;
            }
        }
    }
    return true;
}
