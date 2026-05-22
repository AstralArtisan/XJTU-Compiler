#include "slr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────── */

static bool is_terminal_like(const Grammar *g, int sym) {
    SymKind k = grammar_symbol_kind(g, sym);
    return k == SYM_TERM || k == SYM_END;
}

static bool is_nonterm(const Grammar *g, int sym) {
    return grammar_symbol_kind(g, sym) == SYM_NONTERM;
}

static int find_edge_to(const Lr0Collection *c, int from, int sym) {
    for (int i = 0; i < c->edge_count; i++)
        if (c->edges[i].from == from && c->edges[i].on_symbol == sym)
            return c->edges[i].to;
    return -1;
}

/* ── FIRST 计算（含 nullable） ───────────────────────── */

/* 把 src 集合加入 dst，返回是否有变化（忽略 ε，ε 用 nullable 标记） */
static bool merge_first(bool dst[GR_MAX_SYMBOLS], const bool src[GR_MAX_SYMBOLS],
                        int max_sym) {
    bool changed = false;
    for (int i = 0; i < max_sym; i++) {
        if (src[i] && !dst[i]) {
            dst[i] = true;
            changed = true;
        }
    }
    return changed;
}

void slr_compute_first(const Grammar *g, SlrSets sets[]) {
    for (int i = 0; i < g->symbol_count; i++) {
        memset(&sets[i], 0, sizeof(SlrSets));
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int pi = 0; pi < g->production_count; pi++) {
            const GrProduction *p = &g->productions[pi];
            int A = p->lhs;

            if (p->rhs_len == 0) {
                /* A -> ε */
                if (!sets[A].nullable) {
                    sets[A].nullable = true;
                    changed = true;
                }
                continue;
            }

            bool all_nullable = true;
            for (int k = 0; k < p->rhs_len; k++) {
                int X = p->rhs[k];
                if (is_terminal_like(g, X)) {
                    if (!sets[A].first[X]) {
                        sets[A].first[X] = true;
                        changed = true;
                    }
                    all_nullable = false;
                    break;
                }
                /* nonterm: 加上 FIRST(X) */
                if (merge_first(sets[A].first, sets[X].first, g->symbol_count))
                    changed = true;
                if (!sets[X].nullable) {
                    all_nullable = false;
                    break;
                }
            }
            if (all_nullable && !sets[A].nullable) {
                sets[A].nullable = true;
                changed = true;
            }
        }
    }
}

/* 求 β = X_start..X_{end-1} 的 FIRST，写入 out。返回是否整段都可空（nullable）。 */
static bool first_of_sequence(const Grammar *g, const SlrSets sets[],
                              const int *seq, int start, int end,
                              bool out[GR_MAX_SYMBOLS]) {
    bool all_nullable = true;
    for (int k = start; k < end; k++) {
        int X = seq[k];
        if (is_terminal_like(g, X)) {
            out[X] = true;
            all_nullable = false;
            break;
        }
        for (int s = 0; s < g->symbol_count; s++)
            if (sets[X].first[s]) out[s] = true;
        if (!sets[X].nullable) {
            all_nullable = false;
            break;
        }
    }
    return all_nullable;
}

/* ── FOLLOW 计算 ─────────────────────────────────────── */

void slr_compute_follow(const Grammar *g, SlrSets sets[]) {
    /* FOLLOW(augmented_start) = {$} */
    int aug = g->augmented_start >= 0 ? g->augmented_start : g->start_symbol;
    if (aug >= 0 && g->end_symbol >= 0)
        sets[aug].follow[g->end_symbol] = true;

    bool changed = true;
    while (changed) {
        changed = false;
        for (int pi = 0; pi < g->production_count; pi++) {
            const GrProduction *p = &g->productions[pi];
            int A = p->lhs;
            for (int k = 0; k < p->rhs_len; k++) {
                int B = p->rhs[k];
                if (!is_nonterm(g, B)) continue;

                bool beta_first[GR_MAX_SYMBOLS] = {0};
                bool beta_nullable =
                    first_of_sequence(g, sets, p->rhs, k + 1, p->rhs_len, beta_first);

                /* 把 FIRST(β) - {ε} 加入 FOLLOW(B) */
                for (int s = 0; s < g->symbol_count; s++) {
                    if (beta_first[s] && !sets[B].follow[s]) {
                        sets[B].follow[s] = true;
                        changed = true;
                    }
                }

                /* 若 β 可空（包括 β 空串），FOLLOW(A) ⊆ FOLLOW(B) */
                if (beta_nullable) {
                    for (int s = 0; s < g->symbol_count; s++) {
                        if (sets[A].follow[s] && !sets[B].follow[s]) {
                            sets[B].follow[s] = true;
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}

/* ── 表构造 ──────────────────────────────────────────── */

void slr_init(SlrTable *t, const Grammar *g, const Lr0Collection *lr0) {
    memset(t, 0, sizeof(*t));
    t->g = g;
    t->lr0 = lr0;
    t->is_slr1 = true;
    for (int i = 0; i < LR0_MAX_STATES; i++)
        for (int j = 0; j < GR_MAX_SYMBOLS; j++)
            t->goto_tab[i][j] = -1;
}

static void record_conflict(SlrTable *t, int state, int sym,
                            SlrActionKind existing_kind, int existing_target,
                            SlrActionKind incoming_kind, int incoming_target) {
    if (t->conflict_count >= SLR_MAX_CONFLICTS) return;
    SlrConflict *cf = &t->conflicts[t->conflict_count++];
    cf->state = state;
    cf->on_symbol = sym;
    cf->existing_kind   = existing_kind;
    cf->existing_target = existing_target;
    cf->incoming_kind   = incoming_kind;
    cf->incoming_target = incoming_target;
    t->is_slr1 = false;
}

/* 尝试在 ACTION[state][sym] 写入 act。冲突时记录；shift-reduce 走 prefer-shift
 * （与 yacc/bison 默认一致），reduce-reduce 保留先到的（编号较小的产生式优先）。 */
static void try_set_action(SlrTable *t, int state, int sym, SlrAction act) {
    SlrAction *cell = &t->action[state][sym];
    if (cell->kind == SLR_ACTION_ERROR) {
        *cell = act;
        return;
    }
    if (cell->kind == act.kind && cell->target == act.target) return; /* 同动作幂等 */

    /* shift 优先于 reduce */
    bool existing_is_shift = (cell->kind == SLR_ACTION_SHIFT);
    bool incoming_is_shift = (act.kind  == SLR_ACTION_SHIFT);

    if (existing_is_shift && !incoming_is_shift) {
        /* 保留 shift，记一条 dropped reduce */
        record_conflict(t, state, sym, cell->kind, cell->target, act.kind, act.target);
        return;
    }
    if (!existing_is_shift && incoming_is_shift) {
        /* 用 shift 覆盖已有 reduce；日志中"kept"应当是 shift，"dropped"是 reduce */
        SlrActionKind dropped_kind   = cell->kind;
        int           dropped_target = cell->target;
        *cell = act;
        record_conflict(t, state, sym, act.kind, act.target, dropped_kind, dropped_target);
        return;
    }
    /* 同种动作不同目标：保留先到的，记冲突 */
    record_conflict(t, state, sym, cell->kind, cell->target, act.kind, act.target);
}

bool slr_build(SlrTable *t) {
    const Grammar *g = t->g;
    const Lr0Collection *lr0 = t->lr0;

    slr_compute_first(g, t->sets);
    slr_compute_follow(g, t->sets);

    int aug = g->augmented_start;

    for (int i = 0; i < lr0->state_count; i++) {
        const Lr0ItemSet *I = &lr0->states[i];

        for (int k = 0; k < I->item_count; k++) {
            const Lr0Item it = I->items[k];
            const GrProduction *p = &g->productions[it.prod];

            if (it.dot < p->rhs_len) {
                int X = p->rhs[it.dot];
                int j = find_edge_to(lr0, i, X);
                if (j < 0) continue;
                if (is_terminal_like(g, X)) {
                    SlrAction a = { SLR_ACTION_SHIFT, j };
                    try_set_action(t, i, X, a);
                } else if (is_nonterm(g, X)) {
                    if (t->goto_tab[i][X] < 0) t->goto_tab[i][X] = j;
                    /* GOTO 同状态多次填同样目标是 LR(0) 构造的自然结果 */
                }
            } else {
                /* 归约项目 A -> α . */
                if (p->lhs == aug) {
                    SlrAction a = { SLR_ACTION_ACCEPT, -1 };
                    try_set_action(t, i, g->end_symbol, a);
                } else {
                    for (int s = 0; s < g->symbol_count; s++) {
                        if (!t->sets[p->lhs].follow[s]) continue;
                        SlrAction a = { SLR_ACTION_REDUCE, it.prod };
                        try_set_action(t, i, s, a);
                    }
                }
            }
        }
    }

    return true;
}

/* ── 打印（文本） ────────────────────────────────────── */

static const char *action_short(SlrActionKind k) {
    switch (k) {
        case SLR_ACTION_SHIFT:  return "s";
        case SLR_ACTION_REDUCE: return "r";
        case SLR_ACTION_ACCEPT: return "acc";
        default:                return "";
    }
}

void slr_print_sets(const SlrTable *t, FILE *out) {
    const Grammar *g = t->g;
    fprintf(out, "FIRST sets:\n");
    for (int i = 0; i < g->symbol_count; i++) {
        if (!is_nonterm(g, i)) continue;
        fprintf(out, "  FIRST(%s) = {", grammar_symbol_name(g, i));
        bool first = true;
        if (t->sets[i].nullable) { fprintf(out, "ε"); first = false; }
        for (int s = 0; s < g->symbol_count; s++) {
            if (!t->sets[i].first[s]) continue;
            fprintf(out, "%s%s", first ? "" : ", ", grammar_symbol_name(g, s));
            first = false;
        }
        fprintf(out, "}\n");
    }
    fprintf(out, "\nFOLLOW sets:\n");
    for (int i = 0; i < g->symbol_count; i++) {
        if (!is_nonterm(g, i)) continue;
        fprintf(out, "  FOLLOW(%s) = {", grammar_symbol_name(g, i));
        bool first = true;
        for (int s = 0; s < g->symbol_count; s++) {
            if (!t->sets[i].follow[s]) continue;
            fprintf(out, "%s%s", first ? "" : ", ", grammar_symbol_name(g, s));
            first = false;
        }
        fprintf(out, "}\n");
    }
}

void slr_print_tables(const SlrTable *t, FILE *out) {
    const Grammar *g = t->g;
    const Lr0Collection *lr0 = t->lr0;

    /* 表头：先列终结符（含 $），再列非终结符（不含 S'、ε、$） */
    int terms[GR_MAX_SYMBOLS], term_count = 0;
    int nts[GR_MAX_SYMBOLS],   nt_count = 0;
    for (int i = 0; i < g->symbol_count; i++) {
        if (i == g->eps_symbol) continue;
        if (is_terminal_like(g, i)) terms[term_count++] = i;
        else if (is_nonterm(g, i) && i != g->augmented_start) nts[nt_count++] = i;
    }

    fprintf(out, "ACTION / GOTO table:\n");
    fprintf(out, "%-6s | ", "state");
    for (int i = 0; i < term_count; i++)
        fprintf(out, "%-8s ", grammar_symbol_name(g, terms[i]));
    fprintf(out, "| ");
    for (int i = 0; i < nt_count; i++)
        fprintf(out, "%-6s ", grammar_symbol_name(g, nts[i]));
    fprintf(out, "\n");

    for (int s = 0; s < lr0->state_count; s++) {
        fprintf(out, "I%-5d | ", s);
        for (int i = 0; i < term_count; i++) {
            const SlrAction *a = &t->action[s][terms[i]];
            if (a->kind == SLR_ACTION_ERROR)        fprintf(out, "%-8s ", "");
            else if (a->kind == SLR_ACTION_ACCEPT)  fprintf(out, "%-8s ", "acc");
            else                                    fprintf(out, "%s%-7d ",
                                                            action_short(a->kind), a->target);
        }
        fprintf(out, "| ");
        for (int i = 0; i < nt_count; i++) {
            int g_to = t->goto_tab[s][nts[i]];
            if (g_to < 0) fprintf(out, "%-6s ", "");
            else          fprintf(out, "%-6d ", g_to);
        }
        fprintf(out, "\n");
    }
}

void slr_print_conflicts(const SlrTable *t, FILE *out) {
    const Grammar *g = t->g;
    if (t->conflict_count == 0) {
        fprintf(out, "Conflicts: none (SLR(1) grammar)\n");
        return;
    }
    fprintf(out, "Conflicts: %d (NOT SLR(1))\n", t->conflict_count);
    for (int i = 0; i < t->conflict_count; i++) {
        const SlrConflict *cf = &t->conflicts[i];
        const char *kind_name = "conflict";
        if ((cf->existing_kind == SLR_ACTION_SHIFT && cf->incoming_kind == SLR_ACTION_REDUCE) ||
            (cf->existing_kind == SLR_ACTION_REDUCE && cf->incoming_kind == SLR_ACTION_SHIFT))
            kind_name = "shift-reduce";
        else if (cf->existing_kind == SLR_ACTION_REDUCE && cf->incoming_kind == SLR_ACTION_REDUCE)
            kind_name = "reduce-reduce";
        fprintf(out, "  I%d on '%s' %s: kept %s%d, dropped %s%d\n",
                cf->state, grammar_symbol_name(g, cf->on_symbol), kind_name,
                action_short(cf->existing_kind), cf->existing_target,
                action_short(cf->incoming_kind), cf->incoming_target);
    }
}

void slr_print(const SlrTable *t, FILE *out) {
    grammar_print(t->g, out);
    fputc('\n', out);
    slr_print_sets(t, out);
    fputc('\n', out);
    slr_print_tables(t, out);
    fputc('\n', out);
    slr_print_conflicts(t, out);
}

/* ── JSON 输出 ───────────────────────────────────────── */

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

static const char *action_kind_str(SlrActionKind k) {
    switch (k) {
        case SLR_ACTION_SHIFT:  return "shift";
        case SLR_ACTION_REDUCE: return "reduce";
        case SLR_ACTION_ACCEPT: return "accept";
        default:                return "error";
    }
}

void slr_to_json(const SlrTable *t, FILE *out) {
    const Grammar *g = t->g;
    const Lr0Collection *lr0 = t->lr0;

    int terms[GR_MAX_SYMBOLS], term_count = 0;
    int nts[GR_MAX_SYMBOLS],   nt_count = 0;
    for (int i = 0; i < g->symbol_count; i++) {
        if (i == g->eps_symbol) continue;
        if (is_terminal_like(g, i)) terms[term_count++] = i;
        else if (is_nonterm(g, i) && i != g->augmented_start) nts[nt_count++] = i;
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"is_slr1\": %s,\n", t->is_slr1 ? "true" : "false");

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

    /* terminals / nonterms 索引（方便前端表头） */
    fprintf(out, "  \"terminals\": [");
    for (int i = 0; i < term_count; i++) {
        json_string(out, grammar_symbol_name(g, terms[i]));
        if (i + 1 < term_count) fputc(',', out);
    }
    fprintf(out, "],\n");
    fprintf(out, "  \"nonterminals\": [");
    for (int i = 0; i < nt_count; i++) {
        json_string(out, grammar_symbol_name(g, nts[i]));
        if (i + 1 < nt_count) fputc(',', out);
    }
    fprintf(out, "],\n");

    /* states + items（复用 LR(0) 字段） */
    fprintf(out, "  \"states\": [\n");
    for (int s = 0; s < lr0->state_count; s++) {
        fprintf(out, "    {\"id\":%d,\"items\":[", s);
        for (int i = 0; i < lr0->states[s].item_count; i++) {
            fprintf(out, "{\"prod\":%d,\"dot\":%d}%s",
                    lr0->states[s].items[i].prod,
                    lr0->states[s].items[i].dot,
                    i + 1 < lr0->states[s].item_count ? "," : "");
        }
        fprintf(out, "]}%s\n", s + 1 < lr0->state_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* edges */
    fprintf(out, "  \"edges\": [\n");
    for (int e = 0; e < lr0->edge_count; e++) {
        fprintf(out, "    {\"from\":%d,\"sym\":", lr0->edges[e].from);
        json_string(out, grammar_symbol_name(g, lr0->edges[e].on_symbol));
        fprintf(out, ",\"to\":%d}%s\n", lr0->edges[e].to,
                e + 1 < lr0->edge_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* FIRST / FOLLOW */
    fprintf(out, "  \"first\": [\n");
    for (int i = 0; i < nt_count; i++) {
        int A = nts[i];
        fprintf(out, "    {\"symbol\":");
        json_string(out, grammar_symbol_name(g, A));
        fprintf(out, ",\"nullable\":%s,\"set\":[", t->sets[A].nullable ? "true" : "false");
        bool first = true;
        for (int s = 0; s < g->symbol_count; s++) {
            if (!t->sets[A].first[s]) continue;
            if (!first) fputc(',', out);
            json_string(out, grammar_symbol_name(g, s));
            first = false;
        }
        fprintf(out, "]}%s\n", i + 1 < nt_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    fprintf(out, "  \"follow\": [\n");
    for (int i = 0; i < nt_count; i++) {
        int A = nts[i];
        fprintf(out, "    {\"symbol\":");
        json_string(out, grammar_symbol_name(g, A));
        fprintf(out, ",\"set\":[");
        bool first = true;
        for (int s = 0; s < g->symbol_count; s++) {
            if (!t->sets[A].follow[s]) continue;
            if (!first) fputc(',', out);
            json_string(out, grammar_symbol_name(g, s));
            first = false;
        }
        fprintf(out, "]}%s\n", i + 1 < nt_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* ACTION 表 */
    fprintf(out, "  \"action\": [\n");
    for (int s = 0; s < lr0->state_count; s++) {
        fprintf(out, "    {\"state\":%d,\"row\":[", s);
        bool first = true;
        for (int i = 0; i < term_count; i++) {
            const SlrAction *a = &t->action[s][terms[i]];
            if (a->kind == SLR_ACTION_ERROR) continue;
            if (!first) fputc(',', out);
            fprintf(out, "{\"sym\":");
            json_string(out, grammar_symbol_name(g, terms[i]));
            fprintf(out, ",\"kind\":\"%s\",\"target\":%d}",
                    action_kind_str(a->kind), a->target);
            first = false;
        }
        fprintf(out, "]}%s\n", s + 1 < lr0->state_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* GOTO 表 */
    fprintf(out, "  \"goto\": [\n");
    for (int s = 0; s < lr0->state_count; s++) {
        fprintf(out, "    {\"state\":%d,\"row\":[", s);
        bool first = true;
        for (int i = 0; i < nt_count; i++) {
            int target = t->goto_tab[s][nts[i]];
            if (target < 0) continue;
            if (!first) fputc(',', out);
            fprintf(out, "{\"sym\":");
            json_string(out, grammar_symbol_name(g, nts[i]));
            fprintf(out, ",\"target\":%d}", target);
            first = false;
        }
        fprintf(out, "]}%s\n", s + 1 < lr0->state_count ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* 冲突列表 */
    fprintf(out, "  \"conflicts\": [\n");
    for (int i = 0; i < t->conflict_count; i++) {
        const SlrConflict *cf = &t->conflicts[i];
        const char *kind_name = "conflict";
        if ((cf->existing_kind == SLR_ACTION_SHIFT && cf->incoming_kind == SLR_ACTION_REDUCE) ||
            (cf->existing_kind == SLR_ACTION_REDUCE && cf->incoming_kind == SLR_ACTION_SHIFT))
            kind_name = "shift-reduce";
        else if (cf->existing_kind == SLR_ACTION_REDUCE && cf->incoming_kind == SLR_ACTION_REDUCE)
            kind_name = "reduce-reduce";
        fprintf(out, "    {\"state\":%d,\"sym\":", cf->state);
        json_string(out, grammar_symbol_name(g, cf->on_symbol));
        fprintf(out, ",\"kind\":\"%s\",", kind_name);
        fprintf(out, "\"existing\":{\"kind\":\"%s\",\"target\":%d},",
                action_kind_str(cf->existing_kind), cf->existing_target);
        fprintf(out, "\"incoming\":{\"kind\":\"%s\",\"target\":%d}}%s\n",
                action_kind_str(cf->incoming_kind), cf->incoming_target,
                i + 1 < t->conflict_count ? "," : "");
    }
    fprintf(out, "  ]\n");

    fprintf(out, "}\n");
}
