#include "lr0.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── item set helpers ────────────────────────────────── */

static int item_cmp(const void *a, const void *b) {
    const Lr0Item *x = (const Lr0Item *)a;
    const Lr0Item *y = (const Lr0Item *)b;
    if (x->prod != y->prod) return x->prod - y->prod;
    return x->dot - y->dot;
}

static void itemset_sort(Lr0ItemSet *I) {
    qsort(I->items, (size_t)I->item_count, sizeof(Lr0Item), item_cmp);
}

static bool itemset_contains(const Lr0ItemSet *I, int prod, int dot) {
    for (int i = 0; i < I->item_count; i++)
        if (I->items[i].prod == prod && I->items[i].dot == dot) return true;
    return false;
}

static bool itemset_add(Lr0ItemSet *I, int prod, int dot) {
    if (itemset_contains(I, prod, dot)) return false;
    if (I->item_count >= LR0_MAX_ITEMS) {
        fprintf(stderr, "lr0: item set overflow\n");
        return false;
    }
    I->items[I->item_count].prod = prod;
    I->items[I->item_count].dot  = dot;
    I->item_count++;
    return true;
}

static bool itemset_equal(const Lr0ItemSet *a, const Lr0ItemSet *b) {
    if (a->item_count != b->item_count) return false;
    for (int i = 0; i < a->item_count; i++) {
        if (a->items[i].prod != b->items[i].prod) return false;
        if (a->items[i].dot  != b->items[i].dot)  return false;
    }
    return true;
}

/* ── closure ─────────────────────────────────────────── */

void lr0_closure(const Grammar *g, Lr0ItemSet *I) {
    bool changed = true;
    while (changed) {
        changed = false;
        int n = I->item_count; /* 边遍历边添加，循环内重新读 item_count */
        for (int i = 0; i < n; i++) {
            const Lr0Item it = I->items[i];
            const GrProduction *p = &g->productions[it.prod];
            if (it.dot >= p->rhs_len) continue;
            int B = p->rhs[it.dot];
            if (grammar_symbol_kind(g, B) != SYM_NONTERM) continue;
            for (int j = 0; j < g->production_count; j++) {
                if (g->productions[j].lhs != B) continue;
                if (itemset_add(I, j, 0)) {
                    changed = true;
                    n = I->item_count;
                }
            }
        }
    }
    itemset_sort(I);
}

/* ── goto ────────────────────────────────────────────── */

void lr0_goto(const Grammar *g, const Lr0ItemSet *I, int sym, Lr0ItemSet *out) {
    out->item_count = 0;
    for (int i = 0; i < I->item_count; i++) {
        const Lr0Item it = I->items[i];
        const GrProduction *p = &g->productions[it.prod];
        if (it.dot >= p->rhs_len) continue;
        if (p->rhs[it.dot] != sym) continue;
        itemset_add(out, it.prod, it.dot + 1);
    }
    if (out->item_count > 0) lr0_closure(g, out);
}

/* ── canonical collection ───────────────────────────── */

void lr0_init(Lr0Collection *c, const Grammar *g) {
    memset(c, 0, sizeof(*c));
    c->g = g;
    c->is_lr0 = true;
}

static int collection_find_state(const Lr0Collection *c, const Lr0ItemSet *I) {
    for (int i = 0; i < c->state_count; i++)
        if (itemset_equal(&c->states[i], I)) return i;
    return -1;
}

bool lr0_build(Lr0Collection *c) {
    const Grammar *g = c->g;
    if (g->augmented_start < 0) {
        fprintf(stderr, "lr0: grammar not augmented\n");
        return false;
    }

    /* 找 S' -> S 的产生式编号（grammar_augment 已把它放在 productions[0]） */
    int aug_prod = -1;
    for (int i = 0; i < g->production_count; i++) {
        if (g->productions[i].lhs == g->augmented_start) {
            aug_prod = i;
            break;
        }
    }
    if (aug_prod < 0) {
        fprintf(stderr, "lr0: cannot find augmented production\n");
        return false;
    }

    /* I0 = closure({S' -> . S}) */
    Lr0ItemSet I0 = {0};
    itemset_add(&I0, aug_prod, 0);
    lr0_closure(g, &I0);
    c->states[0] = I0;
    c->state_count = 1;

    /* worklist: 待处理状态编号，用一个简单数组当 FIFO */
    int worklist[LR0_MAX_STATES];
    int wl_head = 0, wl_tail = 0;
    worklist[wl_tail++] = 0;

    while (wl_head < wl_tail) {
        int from = worklist[wl_head++];
        /* 收集当前状态里"点后面"出现过的所有符号，去重 */
        int seen[GR_MAX_SYMBOLS];
        int seen_count = 0;
        for (int i = 0; i < c->states[from].item_count; i++) {
            const Lr0Item it = c->states[from].items[i];
            const GrProduction *p = &g->productions[it.prod];
            if (it.dot >= p->rhs_len) continue;
            int X = p->rhs[it.dot];
            bool dup = false;
            for (int k = 0; k < seen_count; k++) if (seen[k] == X) { dup = true; break; }
            if (!dup) seen[seen_count++] = X;
        }

        for (int s = 0; s < seen_count; s++) {
            int X = seen[s];
            Lr0ItemSet J = {0};
            lr0_goto(g, &c->states[from], X, &J);
            if (J.item_count == 0) continue;

            int to = collection_find_state(c, &J);
            if (to < 0) {
                if (c->state_count >= LR0_MAX_STATES) {
                    fprintf(stderr, "lr0: state count overflow\n");
                    return false;
                }
                to = c->state_count++;
                c->states[to] = J;
                if (wl_tail >= LR0_MAX_STATES) {
                    fprintf(stderr, "lr0: worklist overflow\n");
                    return false;
                }
                worklist[wl_tail++] = to;
            }

            if (c->edge_count >= LR0_MAX_GOTO) {
                fprintf(stderr, "lr0: edge count overflow\n");
                return false;
            }
            c->edges[c->edge_count].from      = from;
            c->edges[c->edge_count].on_symbol = X;
            c->edges[c->edge_count].to        = to;
            c->edge_count++;
        }
    }

    lr0_detect_conflicts(c);
    return true;
}

/* ── conflict detection ──────────────────────────────── */

void lr0_detect_conflicts(Lr0Collection *c) {
    const Grammar *g = c->g;
    c->conflict_count = 0;
    c->is_lr0 = true;

    for (int s = 0; s < c->state_count; s++) {
        const Lr0ItemSet *I = &c->states[s];
        int reduce_items[LR0_MAX_CONFLICT_ITEMS];
        int reduce_count = 0;
        bool has_shift = false;
        int shift_sym = -1;

        for (int i = 0; i < I->item_count; i++) {
            const Lr0Item it = I->items[i];
            const GrProduction *p = &g->productions[it.prod];
            if (it.dot >= p->rhs_len) {
                if (reduce_count < LR0_MAX_CONFLICT_ITEMS)
                    reduce_items[reduce_count++] = i;
            } else {
                int X = p->rhs[it.dot];
                if (grammar_symbol_kind(g, X) == SYM_TERM) {
                    has_shift = true;
                    if (shift_sym < 0) shift_sym = X;
                }
            }
        }

        /* 接受项 S' -> S . 视为 accept 动作而非 reduce 动作，不计入冲突 */
        bool only_accept_reduce = (reduce_count == 1) &&
            (g->productions[I->items[reduce_items[0]].prod].lhs == g->augmented_start);

        bool sr = (reduce_count >= 1 && has_shift) && !only_accept_reduce;
        bool rr = (reduce_count >= 2);

        if (!sr && !rr) continue;

        if (c->conflict_count >= LR0_MAX_STATES) break;
        Lr0Conflict *cf = &c->conflicts[c->conflict_count++];
        cf->state = s;
        cf->kind  = rr ? CONF_REDUCE_REDUCE : CONF_SHIFT_REDUCE;
        cf->item_count = 0;
        cf->on_symbol = has_shift ? shift_sym : -1;
        for (int k = 0; k < reduce_count && cf->item_count < LR0_MAX_CONFLICT_ITEMS; k++)
            cf->items[cf->item_count++] = reduce_items[k];
        c->is_lr0 = false;
    }
}

/* ── pretty print ────────────────────────────────────── */

/* A -> ε 的产生式 rhs_len==0，唯一 item (prod, dot=0) 等价于 "A -> .  "（直接归约）。 */
static void print_item(const Grammar *g, const Lr0Item *it, FILE *out) {
    const GrProduction *p = &g->productions[it->prod];
    fprintf(out, "%s ->", grammar_symbol_name(g, p->lhs));
    if (p->rhs_len == 0) {
        fprintf(out, " .");
        return;
    }
    for (int i = 0; i < p->rhs_len; i++) {
        if (i == it->dot) fprintf(out, " .");
        fprintf(out, " %s", grammar_symbol_name(g, p->rhs[i]));
    }
    if (it->dot == p->rhs_len) fprintf(out, " .");
}

void lr0_print_set(const Grammar *g, const Lr0ItemSet *I, FILE *out) {
    for (int i = 0; i < I->item_count; i++) {
        fputs("    ", out);
        print_item(g, &I->items[i], out);
        fputc('\n', out);
    }
}

void lr0_print_conflicts(const Lr0Collection *c, FILE *out) {
    if (c->conflict_count == 0) {
        fprintf(out, "Conflicts: none (LR(0) grammar)\n");
        return;
    }
    fprintf(out, "Conflicts: %d (NOT LR(0))\n", c->conflict_count);
    for (int i = 0; i < c->conflict_count; i++) {
        const Lr0Conflict *cf = &c->conflicts[i];
        const char *kind = cf->kind == CONF_SHIFT_REDUCE ? "shift-reduce" : "reduce-reduce";
        fprintf(out, "  I%d %s", cf->state, kind);
        if (cf->kind == CONF_SHIFT_REDUCE && cf->on_symbol >= 0)
            fprintf(out, " on '%s'", grammar_symbol_name(c->g, cf->on_symbol));
        fputc('\n', out);
        for (int k = 0; k < cf->item_count; k++) {
            fputs("    reduce: ", out);
            print_item(c->g, &c->states[cf->state].items[cf->items[k]], out);
            fputc('\n', out);
        }
    }
}

void lr0_print(const Lr0Collection *c, FILE *out) {
    grammar_print(c->g, out);
    fputc('\n', out);
    fprintf(out, "Canonical Collection of LR(0) Items: %d states\n\n", c->state_count);

    for (int s = 0; s < c->state_count; s++) {
        fprintf(out, "State I%d:\n", s);
        lr0_print_set(c->g, &c->states[s], out);
        /* 该状态出去的边 */
        bool any_edge = false;
        for (int e = 0; e < c->edge_count; e++) {
            if (c->edges[e].from != s) continue;
            if (!any_edge) { fprintf(out, "  Goto:\n"); any_edge = true; }
            fprintf(out, "    %s -> I%d\n",
                    grammar_symbol_name(c->g, c->edges[e].on_symbol),
                    c->edges[e].to);
        }
        fputc('\n', out);
    }

    lr0_print_conflicts(c, out);
}

/* ── JSON ────────────────────────────────────────────── */

static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:   fputc(*s, out);     break;
        }
    }
    fputc('"', out);
}

void lr0_to_json(const Lr0Collection *c, FILE *out) {
    const Grammar *g = c->g;

    fprintf(out, "{\n");
    fprintf(out, "  \"is_lr0\": %s,\n", c->is_lr0 ? "true" : "false");

    /* symbols */
    fprintf(out, "  \"symbols\": [\n");
    for (int i = 0; i < g->symbol_count; i++) {
        const char *kind =
            g->symbols[i].kind == SYM_TERM    ? "term"    :
            g->symbols[i].kind == SYM_NONTERM ? "nonterm" :
            g->symbols[i].kind == SYM_EPS     ? "eps"     : "end";
        fprintf(out, "    {\"id\":%d,\"name\":", i);
        json_string(out, g->symbols[i].name);
        fprintf(out, ",\"kind\":\"%s\"}%s\n", kind, i + 1 < g->symbol_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* productions */
    fprintf(out, "  \"productions\": [\n");
    for (int i = 0; i < g->production_count; i++) {
        const GrProduction *p = &g->productions[i];
        fprintf(out, "    {\"id\":%d,\"lhs\":", i);
        json_string(out, grammar_symbol_name(g, p->lhs));
        fprintf(out, ",\"rhs\":[");
        for (int j = 0; j < p->rhs_len; j++) {
            json_string(out, grammar_symbol_name(g, p->rhs[j]));
            if (j + 1 < p->rhs_len) fputc(',', out);
        }
        fprintf(out, "]}%s\n", i + 1 < g->production_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* states */
    fprintf(out, "  \"states\": [\n");
    for (int s = 0; s < c->state_count; s++) {
        fprintf(out, "    {\"id\":%d,\"items\":[", s);
        for (int i = 0; i < c->states[s].item_count; i++) {
            fprintf(out, "{\"prod\":%d,\"dot\":%d}%s",
                    c->states[s].items[i].prod,
                    c->states[s].items[i].dot,
                    i + 1 < c->states[s].item_count ? "," : "");
        }
        fprintf(out, "]}%s\n", s + 1 < c->state_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* edges */
    fprintf(out, "  \"edges\": [\n");
    for (int e = 0; e < c->edge_count; e++) {
        fprintf(out, "    {\"from\":%d,\"sym\":", c->edges[e].from);
        json_string(out, grammar_symbol_name(g, c->edges[e].on_symbol));
        fprintf(out, ",\"to\":%d}%s\n", c->edges[e].to, e + 1 < c->edge_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* conflicts */
    fprintf(out, "  \"conflicts\": [\n");
    for (int i = 0; i < c->conflict_count; i++) {
        const Lr0Conflict *cf = &c->conflicts[i];
        const char *kind = cf->kind == CONF_SHIFT_REDUCE ? "shift-reduce" : "reduce-reduce";
        fprintf(out, "    {\"state\":%d,\"kind\":\"%s\"", cf->state, kind);
        if (cf->kind == CONF_SHIFT_REDUCE && cf->on_symbol >= 0) {
            fprintf(out, ",\"on\":");
            json_string(out, grammar_symbol_name(g, cf->on_symbol));
        }
        fprintf(out, ",\"reduce_items\":[");
        for (int k = 0; k < cf->item_count; k++) {
            int idx = cf->items[k];
            fprintf(out, "{\"prod\":%d,\"dot\":%d}%s",
                    c->states[cf->state].items[idx].prod,
                    c->states[cf->state].items[idx].dot,
                    k + 1 < cf->item_count ? "," : "");
        }
        fprintf(out, "]}%s\n", i + 1 < c->conflict_count ? "," : "");
    }
    fprintf(out, "  ]\n");

    fprintf(out, "}\n");
}
