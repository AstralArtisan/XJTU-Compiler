#include "grammar.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────── */

static void trim(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t'))
        *end-- = '\0';
}

static char *skip_ws(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static bool is_arrow(const char *s, int *consumed) {
    if (strncmp(s, "->", 2) == 0)  { *consumed = 2; return true; }
    if (strncmp(s, "::=", 3) == 0) { *consumed = 3; return true; }
    /* "→" 是三字节 UTF-8 序列 0xE2 0x86 0x92 */
    if ((unsigned char)s[0] == 0xE2 &&
        (unsigned char)s[1] == 0x86 &&
        (unsigned char)s[2] == 0x92) { *consumed = 3; return true; }
    return false;
}

static bool is_eps_token(const char *tok) {
    if (strcmp(tok, "epsilon") == 0) return true;
    if (strcmp(tok, "EPSILON") == 0) return true;
    /* "ε" 是两字节 UTF-8 序列 0xCE 0xB5 */
    if ((unsigned char)tok[0] == 0xCE &&
        (unsigned char)tok[1] == 0xB5 &&
        tok[2] == '\0') return true;
    return false;
}

/* ── symbol table ────────────────────────────────────── */

void grammar_init(Grammar *g) {
    memset(g, 0, sizeof(*g));
    g->start_symbol     = -1;
    g->augmented_start  = -1;
    g->end_symbol       = grammar_intern(g, "$",       SYM_END);
    g->eps_symbol       = grammar_intern(g, "epsilon", SYM_EPS);
}

int grammar_lookup(const Grammar *g, const char *name) {
    for (int i = 0; i < g->symbol_count; i++)
        if (strcmp(g->symbols[i].name, name) == 0) return i;
    return -1;
}

int grammar_intern(Grammar *g, const char *name, SymKind kind) {
    int idx = grammar_lookup(g, name);
    if (idx >= 0) {
        /* 升级：先按 TERM 默认登记，后续遇到 LHS 会改为 NONTERM */
        if (kind == SYM_NONTERM && g->symbols[idx].kind == SYM_TERM)
            g->symbols[idx].kind = SYM_NONTERM;
        return idx;
    }
    if (g->symbol_count >= GR_MAX_SYMBOLS) {
        fprintf(stderr, "grammar: symbol table overflow at '%s'\n", name);
        return -1;
    }
    GrSymbol *s = &g->symbols[g->symbol_count];
    strncpy(s->name, name, GR_LABEL_LEN - 1);
    s->name[GR_LABEL_LEN - 1] = '\0';
    s->kind = kind;
    return g->symbol_count++;
}

const char *grammar_symbol_name(const Grammar *g, int sym) {
    if (sym < 0 || sym >= g->symbol_count) return "?";
    return g->symbols[sym].name;
}

SymKind grammar_symbol_kind(const Grammar *g, int sym) {
    if (sym < 0 || sym >= g->symbol_count) return SYM_TERM;
    return g->symbols[sym].kind;
}

/* ── parser ──────────────────────────────────────────── */

typedef struct {
    char terminals[GR_MAX_SYMBOLS][GR_LABEL_LEN];
    int  terminal_count;
    char start_name[GR_LABEL_LEN];
    bool has_start;
} GrammarPrelude;

static void parse_directive(GrammarPrelude *p, char *line) {
    line = skip_ws(line + 1); /* 跳过 '%' */
    if (strncmp(line, "start", 5) == 0) {
        char *name = skip_ws(line + 5);
        char *end = name;
        while (*end && !isspace((unsigned char)*end)) end++;
        *end = '\0';
        strncpy(p->start_name, name, GR_LABEL_LEN - 1);
        p->start_name[GR_LABEL_LEN - 1] = '\0';
        p->has_start = true;
    } else if (strncmp(line, "terminals", 9) == 0) {
        char *rest = skip_ws(line + 9);
        char *tok = strtok(rest, " \t");
        while (tok) {
            if (p->terminal_count < GR_MAX_SYMBOLS) {
                strncpy(p->terminals[p->terminal_count], tok, GR_LABEL_LEN - 1);
                p->terminals[p->terminal_count][GR_LABEL_LEN - 1] = '\0';
                p->terminal_count++;
            }
            tok = strtok(NULL, " \t");
        }
    } else {
        fprintf(stderr, "grammar: unknown directive '%%%s'\n", line);
    }
}

static bool is_declared_terminal(const GrammarPrelude *p, const char *name) {
    for (int i = 0; i < p->terminal_count; i++)
        if (strcmp(p->terminals[i], name) == 0) return true;
    return false;
}

/* 返回 true 表示成功添加 alt 到 productions[] */
static bool add_production(Grammar *g, int lhs, char *alt_text,
                           const GrammarPrelude *prelude, bool terminals_declared) {
    if (g->production_count >= GR_MAX_PRODUCTIONS) {
        fprintf(stderr, "grammar: too many productions\n");
        return false;
    }

    GrProduction *p = &g->productions[g->production_count];
    p->lhs = lhs;
    p->rhs_len = 0;

    char *tok = strtok(alt_text, " \t");
    while (tok) {
        if (is_eps_token(tok)) {
            /* ε 不进入 rhs，rhs_len = 0 表示空产生式 */
            tok = strtok(NULL, " \t");
            continue;
        }
        if (p->rhs_len >= GR_MAX_RHS) {
            fprintf(stderr, "grammar: rhs too long for '%s'\n",
                    grammar_symbol_name(g, lhs));
            return false;
        }
        SymKind kind = SYM_TERM;
        if (terminals_declared) {
            kind = is_declared_terminal(prelude, tok) ? SYM_TERM : SYM_NONTERM;
        } else {
            /* 惯例回退：首字符大写认为是非终结符的弱推断；
             * 真正决定权交给 LHS 出现 ——
             * 凡作为 LHS 出现过的符号会被升级为 NONTERM。
             * 这里先按弱推断登记，后面 grammar_load 收尾会走第二遍补正。*/
            if (isupper((unsigned char)tok[0]) && strlen(tok) == 1) kind = SYM_NONTERM;
        }
        int s = grammar_intern(g, tok, kind);
        if (s < 0) return false;
        p->rhs[p->rhs_len++] = s;
        tok = strtok(NULL, " \t");
    }

    g->production_count++;
    return true;
}

static bool parse_rule_line(Grammar *g, char *line,
                            const GrammarPrelude *prelude, bool terminals_declared) {
    int arrow_at = -1, arrow_len = 0;
    for (int i = 0; line[i]; i++) {
        int n;
        if (is_arrow(&line[i], &n)) { arrow_at = i; arrow_len = n; break; }
    }
    if (arrow_at < 0) {
        fprintf(stderr, "grammar: no arrow in rule: %s\n", line);
        return false;
    }

    line[arrow_at] = '\0';
    char *lhs_text = skip_ws(line);
    char *end = lhs_text + strlen(lhs_text) - 1;
    while (end >= lhs_text && isspace((unsigned char)*end)) *end-- = '\0';

    int lhs = grammar_intern(g, lhs_text, SYM_NONTERM);
    if (lhs < 0) return false;
    if (g->start_symbol < 0) g->start_symbol = lhs;

    char *rhs_text = skip_ws(line + arrow_at + arrow_len);

    /* 按 '|' 切分多个 alt */
    char *alt_start = rhs_text;
    for (char *q = rhs_text; ; q++) {
        if (*q == '|' || *q == '\0') {
            char saved = *q;
            *q = '\0';
            char *alt = skip_ws(alt_start);
            char *ae = alt + strlen(alt) - 1;
            while (ae >= alt && isspace((unsigned char)*ae)) *ae-- = '\0';
            if (!add_production(g, lhs, alt, prelude, terminals_declared))
                return false;
            if (saved == '\0') break;
            alt_start = q + 1;
        }
    }
    return true;
}

bool grammar_load(Grammar *g, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror(filename); return false; }

    GrammarPrelude prelude = {0};
    char line[1024];
    bool seen_rule = false;

    /* 第一遍：扫描 directives 与产生式 */
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        char *p = skip_ws(line);
        if (*p == '\0' || *p == '#') continue;

        if (*p == '%' && !seen_rule) {
            parse_directive(&prelude, p);
            continue;
        }

        if (!parse_rule_line(g, p, &prelude, prelude.terminal_count > 0)) {
            fclose(fp);
            return false;
        }
        seen_rule = true;
    }
    fclose(fp);

    /* 应用 %start */
    if (prelude.has_start) {
        int s = grammar_lookup(g, prelude.start_name);
        if (s < 0) {
            fprintf(stderr, "grammar: %%start symbol '%s' not in grammar\n",
                    prelude.start_name);
            return false;
        }
        g->start_symbol = s;
    }

    if (g->start_symbol < 0) {
        fprintf(stderr, "grammar: no productions\n");
        return false;
    }

    /* 第二遍：把所有出现在 LHS 的符号确保为 NONTERM */
    for (int i = 0; i < g->production_count; i++) {
        int lhs = g->productions[i].lhs;
        g->symbols[lhs].kind = SYM_NONTERM;
    }

    return true;
}

/* ── augmentation ────────────────────────────────────── */

void grammar_augment(Grammar *g) {
    if (g->augmented_start >= 0) return;

    char aug_name[GR_LABEL_LEN];
    snprintf(aug_name, sizeof(aug_name), "%s'", grammar_symbol_name(g, g->start_symbol));
    int sp = grammar_intern(g, aug_name, SYM_NONTERM);
    if (sp < 0) return;
    g->augmented_start = sp;

    /* 把 S' -> S 插到 productions[0]，整体右移一位 */
    if (g->production_count >= GR_MAX_PRODUCTIONS) {
        fprintf(stderr, "grammar: cannot augment, productions full\n");
        return;
    }
    for (int i = g->production_count; i > 0; i--)
        g->productions[i] = g->productions[i - 1];
    g->productions[0].lhs = sp;
    g->productions[0].rhs[0] = g->start_symbol;
    g->productions[0].rhs_len = 1;
    g->production_count++;
}

/* ── debug print ─────────────────────────────────────── */

void grammar_print(const Grammar *g, FILE *out) {
    fprintf(out, "Grammar (augmented):\n");
    for (int i = 0; i < g->production_count; i++) {
        const GrProduction *p = &g->productions[i];
        fprintf(out, "  %d: %s ->", i, grammar_symbol_name(g, p->lhs));
        if (p->rhs_len == 0) {
            fprintf(out, " epsilon");
        } else {
            for (int j = 0; j < p->rhs_len; j++)
                fprintf(out, " %s", grammar_symbol_name(g, p->rhs[j]));
        }
        fputc('\n', out);
    }
    fprintf(out, "\nSymbols:\n");
    for (int i = 0; i < g->symbol_count; i++) {
        const char *kind_name =
            g->symbols[i].kind == SYM_TERM    ? "term"    :
            g->symbols[i].kind == SYM_NONTERM ? "nonterm" :
            g->symbols[i].kind == SYM_EPS     ? "eps"     : "end";
        fprintf(out, "  %d: %-12s [%s]\n", i, g->symbols[i].name, kind_name);
    }
    fprintf(out, "\nStart: %s  Augmented: %s\n",
            grammar_symbol_name(g, g->start_symbol),
            g->augmented_start >= 0 ? grammar_symbol_name(g, g->augmented_start) : "(none)");
}
