#include "dfa.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* ── helpers ─────────────────────────────────────────── */

static void trim(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == '\n' || *end == '\r' || *end == ' '))
        *end-- = '\0';
}

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ── init ────────────────────────────────────────────── */

void dfa_init(DFA *dfa) {
    memset(dfa, 0, sizeof(*dfa));
    memset(dfa->char_to_class, -1, sizeof(dfa->char_to_class));
    dfa->extended = false;
}

/* ── legacy format loader (lab1 .dfa) ────────────────── */

static bool load_legacy(DFA *dfa, FILE *fp, const char *first_line) {
    strncpy(dfa->alphabet, first_line, DFA_MAX_SYMBOLS);
    dfa->alphabet[DFA_MAX_SYMBOLS] = '\0';
    dfa->symbol_count = (int)strlen(dfa->alphabet);

    char line[512];

    if (!fgets(line, sizeof(line), fp)) return false;
    dfa->state_count = atoi(line);
    if (dfa->state_count <= 0 || dfa->state_count >= DFA_MAX_STATES) return false;

    if (!fgets(line, sizeof(line), fp)) return false;
    dfa->start_state = atoi(line);

    if (!fgets(line, sizeof(line), fp)) return false;
    trim(line);
    dfa->final_count = 0;
    char *tok = strtok(line, " \t");
    while (tok) {
        if (dfa->final_count >= DFA_MAX_FINALS) return false;
        dfa->final_states[dfa->final_count++] = atoi(tok);
        tok = strtok(NULL, " \t");
    }

    for (int i = 1; i <= dfa->state_count; i++) {
        if (!fgets(line, sizeof(line), fp)) return false;
        trim(line);
        char *t = strtok(line, " \t");
        for (int j = 0; j < dfa->symbol_count; j++) {
            if (!t) return false;
            dfa->transitions[i][j] = atoi(t);
            t = strtok(NULL, " \t");
        }
    }

    for (int i = 0; i < dfa->symbol_count; i++)
        dfa->char_to_class[(unsigned char)dfa->alphabet[i]] = i;

    dfa->extended = false;
    return true;
}

/* ── extended format loader ──────────────────────────── */

static void parse_char_range(DfaCharClass *cc, const char *spec) {
    int len = (int)strlen(spec);
    if (len == 3 && spec[1] == '-') {
        for (int c = (unsigned char)spec[0]; c <= (unsigned char)spec[2]; c++)
            cc->chars[cc->char_count++] = (char)c;
    } else if (len == 1) {
        cc->chars[cc->char_count++] = spec[0];
    } else if (len == 2 && spec[0] == '\\') {
        switch (spec[1]) {
            case 's': cc->chars[cc->char_count++] = ' ';
                      cc->chars[cc->char_count++] = '\t'; break;
            case 'n': cc->chars[cc->char_count++] = '\n'; break;
            default:  cc->chars[cc->char_count++] = spec[1]; break;
        }
    } else {
        for (int i = 0; i < len; i++)
            cc->chars[cc->char_count++] = spec[i];
    }
}

static bool load_extended(DFA *dfa, FILE *fp) {
    char line[512];
    enum { SEC_NONE, SEC_CHARCLASS, SEC_TRANS, SEC_ACCEPT, SEC_KEYWORDS } section = SEC_NONE;

    dfa->state_count = 0;
    dfa->extended = true;

    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        if (starts_with(line, "CHARCLASS:")) { section = SEC_CHARCLASS; continue; }
        if (starts_with(line, "STATES:"))    { dfa->state_count = atoi(line + 7); continue; }
        if (starts_with(line, "START:"))     { dfa->start_state = atoi(line + 6); continue; }
        if (starts_with(line, "ACCEPT:"))    { section = SEC_ACCEPT; continue; }
        if (starts_with(line, "TRANS:"))     { section = SEC_TRANS; continue; }
        if (starts_with(line, "KEYWORDS:"))  { section = SEC_KEYWORDS; continue; }
        if (strcmp(line, "END") == 0)         { section = SEC_NONE; continue; }

        switch (section) {
        case SEC_CHARCLASS: {
            if (dfa->class_count >= DFA_MAX_CLASSES) break;
            DfaCharClass *cc = &dfa->classes[dfa->class_count];
            cc->char_count = 0;
            char *t = strtok(line, " \t");
            if (!t) break;
            strncpy(cc->name, t, DFA_LABEL_LEN - 1);
            while ((t = strtok(NULL, " \t")))
                parse_char_range(cc, t);
            int idx = dfa->class_count;
            for (int i = 0; i < cc->char_count; i++)
                dfa->char_to_class[(unsigned char)cc->chars[i]] = idx;
            dfa->class_count++;
            break;
        }
        case SEC_ACCEPT: {
            char *t = strtok(line, " \t");
            while (t) {
                char *eq = strchr(t, '=');
                if (eq) {
                    *eq = '\0';
                    int st = atoi(t);
                    if (st >= 0 && st < DFA_MAX_STATES) {
                        strncpy(dfa->accept_labels[st], eq + 1, DFA_LABEL_LEN - 1);
                        bool found = false;
                        for (int i = 0; i < dfa->final_count; i++)
                            if (dfa->final_states[i] == st) { found = true; break; }
                        if (!found && dfa->final_count < DFA_MAX_FINALS)
                            dfa->final_states[dfa->final_count++] = st;
                    }
                }
                t = strtok(NULL, " \t");
            }
            break;
        }
        case SEC_TRANS: {
            int from, to;
            char sym[DFA_LABEL_LEN];
            if (sscanf(line, "%d %31s -> %d", &from, sym, &to) == 3) {
                int cls = -1;
                for (int i = 0; i < dfa->class_count; i++) {
                    if (strcmp(dfa->classes[i].name, sym) == 0) { cls = i; break; }
                }
                if (cls >= 0 && from >= 0 && from < DFA_MAX_STATES)
                    dfa->transitions[from][cls] = to;
            }
            break;
        }
        case SEC_KEYWORDS: {
            char *t = strtok(line, " \t");
            while (t && dfa->keyword_count < DFA_MAX_KEYWORDS) {
                char *eq = strchr(t, '=');
                if (eq) {
                    *eq = '\0';
                    DfaKeyword *kw = &dfa->keywords[dfa->keyword_count++];
                    strncpy(kw->word, t, DFA_LABEL_LEN - 1);
                    strncpy(kw->token_name, eq + 1, DFA_LABEL_LEN - 1);
                }
                t = strtok(NULL, " \t");
            }
            break;
        }
        default: break;
        }
    }

    dfa->symbol_count = dfa->class_count;
    return dfa->state_count > 0;
}

/* ── dfa_load: auto-detect format ────────────────────── */

bool dfa_load(DFA *dfa, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "cannot open: %s\n", filename);
        return false;
    }

    char first[512];
    while (fgets(first, sizeof(first), fp)) {
        trim(first);
        if (first[0] != '#' && first[0] != '\0') break;
    }

    bool ok;
    if (starts_with(first, "CHARCLASS:") || starts_with(first, "STATES:") ||
        starts_with(first, "START:")) {
        fseek(fp, 0, SEEK_SET);
        ok = load_extended(dfa, fp);
    } else {
        ok = load_legacy(dfa, fp, first);
    }

    fclose(fp);
    return ok;
}

/* ── validate ────────────────────────────────────────── */

bool dfa_validate(const DFA *dfa) {
    if (dfa->state_count <= 0) {
        fprintf(stderr, "state count must be positive\n");
        return false;
    }
    if (dfa->symbol_count <= 0) {
        fprintf(stderr, "symbol count must be positive\n");
        return false;
    }
    int lo = dfa->extended ? 0 : 1;
    int hi = dfa->extended ? dfa->state_count - 1 : dfa->state_count;
    if (dfa->start_state < lo || dfa->start_state > hi) {
        fprintf(stderr, "start state %d out of range [%d,%d]\n",
                dfa->start_state, lo, hi);
        return false;
    }
    if (dfa->final_count <= 0) {
        fprintf(stderr, "no accept states\n");
        return false;
    }
    for (int i = 0; i < dfa->final_count; i++) {
        if (dfa->final_states[i] < lo || dfa->final_states[i] > hi) {
            fprintf(stderr, "accept state %d out of range\n", dfa->final_states[i]);
            return false;
        }
    }
    for (int s = lo; s <= hi; s++) {
        for (int c = 0; c < dfa->symbol_count; c++) {
            int next = dfa->transitions[s][c];
            if (dfa->extended && next == 0 && s != 0) continue;
            if (next < lo || next > hi) {
                fprintf(stderr, "transition from state %d on symbol %d points outside [%d,%d]\n",
                        s, c, lo, hi);
                return false;
            }
        }
    }
    return true;
}

/* ── print ───────────────────────────────────────────── */

void dfa_print(const DFA *dfa, FILE *out) {
    int lo = dfa->extended ? 0 : 1;
    int hi = dfa->extended ? dfa->state_count - 1 : dfa->state_count;

    if (dfa->extended) {
        fprintf(out, "Character classes (%d):\n", dfa->class_count);
        for (int i = 0; i < dfa->class_count; i++)
            fprintf(out, "  [%d] %s (%d chars)\n", i,
                    dfa->classes[i].name, dfa->classes[i].char_count);
    } else {
        fprintf(out, "Alphabet: %s\n", dfa->alphabet);
    }
    fprintf(out, "States: %d  Start: %d  Accept:", dfa->state_count, dfa->start_state);
    for (int i = 0; i < dfa->final_count; i++) {
        fprintf(out, " %d", dfa->final_states[i]);
        if (dfa->accept_labels[dfa->final_states[i]][0])
            fprintf(out, "(%s)", dfa->accept_labels[dfa->final_states[i]]);
    }
    fprintf(out, "\nTransitions:\n");
    for (int s = lo; s <= hi; s++) {
        fprintf(out, "  %d:", s);
        for (int c = 0; c < dfa->symbol_count; c++) {
            int t = dfa->transitions[s][c];
            if (t != 0 || !dfa->extended)
                fprintf(out, " %d", t);
            else
                fprintf(out, " -");
        }
        fprintf(out, "\n");
    }
    if (dfa->keyword_count > 0) {
        fprintf(out, "Keywords:");
        for (int i = 0; i < dfa->keyword_count; i++)
            fprintf(out, " %s=%s", dfa->keywords[i].word, dfa->keywords[i].token_name);
        fprintf(out, "\n");
    }
}

/* ── lab2 bridge: single-step transitions ────────────── */

int dfa_start(const DFA *dfa) {
    return dfa->start_state;
}

int dfa_step(const DFA *dfa, int state, int ch) {
    if (ch < 0) return -1;
    int cls;
    if (dfa->extended) {
        cls = dfa->char_to_class[(unsigned char)ch];
        if (cls < 0) return -1;
    } else {
        cls = -1;
        for (int i = 0; i < dfa->symbol_count; i++) {
            if (dfa->alphabet[i] == (char)ch) { cls = i; break; }
        }
        if (cls < 0) return -1;
    }
    int lo = dfa->extended ? 0 : 1;
    int hi = dfa->extended ? dfa->state_count - 1 : dfa->state_count;
    if (state < lo || state > hi) return -1;
    int next = dfa->transitions[state][cls];
    if (dfa->extended && next == 0 && state != 0) return -1;
    return next;
}

bool dfa_is_accept(const DFA *dfa, int state) {
    for (int i = 0; i < dfa->final_count; i++)
        if (dfa->final_states[i] == state) return true;
    return false;
}

const char *dfa_accept_label(const DFA *dfa, int state) {
    if (state < 0 || state >= DFA_MAX_STATES) return NULL;
    if (dfa->accept_labels[state][0] == '\0') return NULL;
    return dfa->accept_labels[state];
}

const char *dfa_find_keyword(const DFA *dfa, const char *word) {
    for (int i = 0; i < dfa->keyword_count; i++)
        if (strcmp(dfa->keywords[i].word, word) == 0)
            return dfa->keywords[i].token_name;
    return NULL;
}

/* ── lab1 core: simulate ─────────────────────────────── */

bool dfa_simulate(const DFA *dfa, const char *input) {
    int state = dfa->start_state;
    for (int i = 0; input[i]; i++) {
        state = dfa_step(dfa, state, (unsigned char)input[i]);
        if (state < 0) return false;
    }
    return dfa_is_accept(dfa, state);
}

bool dfa_trace(const DFA *dfa, const char *input, FILE *out) {
    int state = dfa->start_state;
    fprintf(out, "start: %d\n", state);
    for (int i = 0; input[i]; i++) {
        int next = dfa_step(dfa, state, (unsigned char)input[i]);
        if (next < 0) {
            fprintf(out, "%d --%c--> ERROR\n", state, input[i]);
            fprintf(out, "result: REJECT\n");
            return false;
        }
        fprintf(out, "%d --%c--> %d\n", state, input[i], next);
        state = next;
    }
    bool ok = dfa_is_accept(dfa, state);
    fprintf(out, "final: %d\n", state);
    fprintf(out, "result: %s\n", ok ? "ACCEPT" : "REJECT");
    return ok;
}

/* ── lab1 core: enumerate ────────────────────────────── */

static void enum_rec(const DFA *dfa, int state, int depth, int target_len,
                     char *buf, int *count, FILE *out) {
    if (depth == target_len) {
        if (!dfa_is_accept(dfa, state)) return;
        buf[depth] = '\0';
        if (depth == 0)
            fprintf(out, "<epsilon>\n");
        else
            fprintf(out, "%s\n", buf);
        (*count)++;
        return;
    }

    if (!dfa->extended) {
        for (int i = 0; i < dfa->symbol_count; i++) {
            int next = dfa->transitions[state][i];
            buf[depth] = dfa->alphabet[i];
            enum_rec(dfa, next, depth + 1, target_len, buf, count, out);
        }
    } else {
        for (int i = 0; i < dfa->class_count; i++) {
            int next = dfa->transitions[state][i];
            if (next == 0 && state != 0) continue;
            char rep = dfa->classes[i].chars[0];
            buf[depth] = rep;
            enum_rec(dfa, next, depth + 1, target_len, buf, count, out);
        }
    }
}

void dfa_enumerate(const DFA *dfa, int max_len, FILE *out) {
    char buf[256];
    int count = 0;
    for (int len = 0; len <= max_len; len++)
        enum_rec(dfa, dfa->start_state, 0, len, buf, &count, out);
    fprintf(out, "Total: %d strings\n", count);
}

/* ── JSON output for frontend ────────────────────────── */

static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:   fputc(*s, out);     break;
        }
    }
    fputc('"', out);
}

void dfa_to_json(const DFA *dfa, FILE *out) {
    int lo = dfa->extended ? 0 : 1;
    int hi = dfa->extended ? dfa->state_count - 1 : dfa->state_count;

    fprintf(out, "{\n  \"extended\": %s,\n", dfa->extended ? "true" : "false");
    fprintf(out, "  \"states\": %d,\n", dfa->state_count);
    fprintf(out, "  \"start\": %d,\n", dfa->start_state);

    fprintf(out, "  \"accept\": [");
    for (int i = 0; i < dfa->final_count; i++) {
        if (i) fputc(',', out);
        fprintf(out, "%d", dfa->final_states[i]);
    }
    fprintf(out, "],\n");

    fprintf(out, "  \"accept_labels\": {");
    bool first = true;
    for (int i = 0; i < dfa->final_count; i++) {
        int s = dfa->final_states[i];
        if (dfa->accept_labels[s][0]) {
            if (!first) fputc(',', out);
            fprintf(out, "\"%d\":", s);
            json_str(out, dfa->accept_labels[s]);
            first = false;
        }
    }
    fprintf(out, "},\n");

    if (dfa->extended) {
        fprintf(out, "  \"classes\": [");
        for (int i = 0; i < dfa->class_count; i++) {
            if (i) fputc(',', out);
            json_str(out, dfa->classes[i].name);
        }
        fprintf(out, "],\n");
    } else {
        fprintf(out, "  \"alphabet\": ");
        json_str(out, dfa->alphabet);
        fprintf(out, ",\n");
    }

    fprintf(out, "  \"transitions\": [\n");
    for (int s = lo; s <= hi; s++) {
        fprintf(out, "    [");
        for (int c = 0; c < dfa->symbol_count; c++) {
            if (c) fputc(',', out);
            fprintf(out, "%d", dfa->transitions[s][c]);
        }
        fprintf(out, "]%s\n", s < hi ? "," : "");
    }
    fprintf(out, "  ],\n");

    fprintf(out, "  \"keywords\": {");
    for (int i = 0; i < dfa->keyword_count; i++) {
        if (i) fputc(',', out);
        json_str(out, dfa->keywords[i].word);
        fputc(':', out);
        json_str(out, dfa->keywords[i].token_name);
    }
    fprintf(out, "}\n}\n");
}
