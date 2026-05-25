#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "token.h"
#include "scanner.h"
#include "table_scanner.h"
#include "dfa.h"
#include "grammar.h"
#include "lr0.h"
#include "slr.h"
#include "ast.h"
#include "symtab.h"
#include "parser.h"
#include "semantic.h"
#include "ir.h"
#include "memmap.h"
#include "asm_arm64.h"

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LEXER_DFA "data/lexer.dfa"

typedef enum {
    SCAN_IMPL_TABLE,
    SCAN_IMPL_HAND
} ScanImpl;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static bool load_table_dfa(DFA *dfa, const char *path) {
    dfa_init(dfa);
    return dfa_load(dfa, path) && dfa_validate(dfa);
}

static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default: fputc(*s, out); break;
        }
    }
    fputc('"', out);
}

static void print_token_text(FILE *out, const Token *t) {
    fprintf(out, "(%s, %s) @%d:%d\n", token_name(t->kind), t->lexeme, t->line, t->col);
}

static void print_token_json(FILE *out, const Token *t) {
    fprintf(out, "{\"kind\":\"%s\",\"lexeme\":", token_name(t->kind));
    json_string(out, t->lexeme);
    fprintf(out, ",\"line\":%d,\"col\":%d}", t->line, t->col);
}

typedef struct {
    union { TableScanner ts; Scanner hand; } u;
    bool (*next)(void *ctx, Token *t);
    int *error_count;
    const char *label;
} ScanContext;

static bool wrap_table_next(void *ctx, Token *t) { return ts_next((TableScanner *)ctx, t); }
static bool wrap_hand_next(void *ctx, Token *t) { return scanner_next((Scanner *)ctx, t); }

static void scan_ctx_init(ScanContext *sc, ScanImpl impl, const DFA *dfa, const char *src) {
    if (impl == SCAN_IMPL_TABLE) {
        ts_init(&sc->u.ts, dfa, src);
        sc->next = wrap_table_next;
        sc->error_count = &sc->u.ts.error_count;
        sc->label = "table-scanner";
    } else {
        scanner_init(&sc->u.hand, src);
        sc->next = wrap_hand_next;
        sc->error_count = &sc->u.hand.error_count;
        sc->label = "hand-scanner";
    }
}

static void scan_text(ScanContext *sc, FILE *out) {
    Token t;
    int count = 0;
    while (sc->next(&sc->u, &t)) {
        print_token_text(out, &t);
        count++;
    }
    fprintf(stderr, "[%s] %d tokens, %d errors\n", sc->label, count, *sc->error_count);
}

static void scan_json(ScanContext *sc, FILE *out) {
    Token t;
    bool first = true;
    fprintf(out, "[\n");
    while (sc->next(&sc->u, &t)) {
        if (!first) fprintf(out, ",\n");
        fputs("  ", out);
        print_token_json(out, &t);
        first = false;
    }
    fprintf(out, "\n]\n");
}

static int scan_mode_1(ScanImpl impl, const DFA *dfa) {
    int n = 0;
    if (scanf("%d", &n) != 1) return 1;
    for (int i = 0; i < n; i++) {
        char buf[TOKEN_LEXEME_LIMIT];
        if (scanf("%127s", buf) != 1) return 1;
        ScanContext sc;
        scan_ctx_init(&sc, impl, dfa, buf);
        Token t;
        bool ok = sc.next(&sc.u, &t);
        printf("%s\n", ok && t.kind != TK_EOF ? token_name(t.kind) : "ERR");
    }
    return 0;
}

static int scan_mode_2(ScanImpl impl, const DFA *dfa) {
    char line[4096];
    int c = getchar();
    while (c == '\n' || c == '\r') c = getchar();
    if (c == EOF) return 0;

    size_t len = 0;
    while (c != EOF && c != '\n') {
        if (len < sizeof(line) - 1) line[len++] = (char)c;
        c = getchar();
    }
    line[len] = '\0';

    ScanContext sc;
    scan_ctx_init(&sc, impl, dfa, line);
    Token t;
    while (sc.next(&sc.u, &t)) printf("%s\n", token_name(t.kind));
    return 0;
}

static bool same_token(const Token *a, const Token *b) {
    return a->kind == b->kind &&
           a->line == b->line &&
           a->col == b->col &&
           strcmp(a->lexeme, b->lexeme) == 0;
}

static int compare_scanners(const char *src, const DFA *dfa) {
    Scanner hand;
    TableScanner table;
    Token h, t;
    int count = 0;
    scanner_init(&hand, src);
    ts_init(&table, dfa, src);

    for (;;) {
        bool has_h = scanner_next(&hand, &h);
        bool has_t = ts_next(&table, &t);
        if (has_h != has_t) {
            fprintf(stderr, "scanner length mismatch after %d tokens\n", count);
            return 1;
        }
        if (!has_h) break;
        count++;
        if (!same_token(&h, &t)) {
            fprintf(stderr, "scanner mismatch at token %d\n", count);
            fprintf(stderr, "  hand : ");
            print_token_text(stderr, &h);
            fprintf(stderr, "  table: ");
            print_token_text(stderr, &t);
            return 1;
        }
    }

    if (hand.error_count != table.error_count) {
        fprintf(stderr, "scanner error-count mismatch: hand=%d table=%d\n",
                hand.error_count, table.error_count);
        return 1;
    }

    printf("IDENTICAL: %d tokens, %d errors\n", count, hand.error_count);
    return 0;
}

static int cmd_scan(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    const char *table_path = DEFAULT_LEXER_DFA;
    ScanImpl impl = SCAN_IMPL_TABLE;
    bool json = false;
    bool compare = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) {
            table_path = argv[++i];
            impl = SCAN_IMPL_TABLE;
        } else if (strcmp(argv[i], "--impl=table") == 0) {
            impl = SCAN_IMPL_TABLE;
        } else if (strcmp(argv[i], "--impl=hand") == 0) {
            impl = SCAN_IMPL_HAND;
        } else if (strcmp(argv[i], "--compare") == 0) {
            compare = true;
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
        }
    }

    DFA table_dfa;
    const DFA *dfa = NULL;
    if (impl == SCAN_IMPL_TABLE || compare) {
        if (!load_table_dfa(&table_dfa, table_path)) return 1;
        dfa = &table_dfa;
    }

    if (compare) {
        if (!in_path) {
            fprintf(stderr, "scan --compare requires -f IN\n");
            return 1;
        }
        char *src = read_file(in_path);
        if (!src) return 1;
        int rc = compare_scanners(src, dfa);
        free(src);
        return rc;
    }

    if (in_path) {
        char *src = read_file(in_path);
        if (!src) return 1;
        FILE *out = stdout;
        if (out_path) {
            out = fopen(out_path, "w");
            if (!out) { perror(out_path); free(src); return 1; }
        }

        ScanContext sc;
        scan_ctx_init(&sc, impl, dfa, src);
        if (json) scan_json(&sc, out);
        else scan_text(&sc, out);

        if (out != stdout) fclose(out);
        free(src);
        return 0;
    }

    int mode = 0;
    if (scanf("%d", &mode) != 1) return 1;
    if (mode == 1) return scan_mode_1(impl, dfa);
    if (mode == 2) return scan_mode_2(impl, dfa);
    fprintf(stderr, "unknown mode: %d\n", mode);
    return 1;
}

static int cmd_dfa(int argc, char **argv) {
    const char *dfa_path = NULL;
    int enumerate_len = -1;
    const char *test_str = NULL;
    bool json = false;
    bool trace = false;
    bool interactive = true;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--enumerate") == 0 && i + 1 < argc) {
            enumerate_len = atoi(argv[++i]);
            interactive = false;
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            test_str = argv[++i];
            interactive = false;
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
            interactive = false;
        } else if (argv[i][0] != '-') {
            dfa_path = argv[i];
        }
    }

    if (!dfa_path) {
        fprintf(stderr, "usage: compiler dfa <file.dfa> [--enumerate N] [--test STR] [--trace] [--format=json]\n");
        return 1;
    }
    if (trace && !test_str) {
        fprintf(stderr, "--trace requires --test STR\n");
        return 1;
    }

    DFA dfa;
    dfa_init(&dfa);
    if (!dfa_load(&dfa, dfa_path)) return 1;
    if (!dfa_validate(&dfa)) return 1;

    if (json) { dfa_to_json(&dfa, stdout); return 0; }
    if (enumerate_len >= 0) { dfa_enumerate(&dfa, enumerate_len, stdout); return 0; }

    if (test_str) {
        bool ok = trace ? dfa_trace(&dfa, test_str, stdout) : dfa_simulate(&dfa, test_str);
        if (!trace) printf("%s: %s\n", test_str, ok ? "ACCEPT" : "REJECT");
        return ok ? 0 : 1;
    }

    if (interactive) {
        dfa_print(&dfa, stdout);
        printf("\nMax length to enumerate: ");
        int n;
        if (scanf("%d", &n) == 1) dfa_enumerate(&dfa, n, stdout);
        while (getchar() != '\n') {}
        printf("\nEnter string to test (empty line to quit): ");
        char buf[256];
        while (fgets(buf, sizeof(buf), stdin)) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
            if (len == 0) break;
            dfa_trace(&dfa, buf, stdout);
            printf("Enter string to test (empty line to quit): ");
        }
    }
    return 0;
}

static int cmd_lr0(int argc, char **argv) {
    const char *grammar_path = NULL;
    const char *out_path     = NULL;
    const char *show         = "all";
    bool json = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
        } else if (strncmp(argv[i], "--show=", 7) == 0) {
            show = argv[i] + 7;
        } else if (argv[i][0] != '-') {
            grammar_path = argv[i];
        }
    }

    if (!grammar_path) {
        fprintf(stderr, "usage: compiler lr0 <grammar_file> "
                        "[--show=closure|goto|conflicts|productions|all] "
                        "[--format=json] [-o OUT]\n");
        return 1;
    }

    Grammar g;
    grammar_init(&g);
    if (!grammar_load(&g, grammar_path)) return 1;
    grammar_augment(&g);

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); return 1; }
    }

    if (strcmp(show, "productions") == 0) {
        grammar_print(&g, out);
        if (out != stdout) fclose(out);
        return 0;
    }

    Lr0Collection c;
    lr0_init(&c, &g);
    if (!lr0_build(&c)) {
        if (out != stdout) fclose(out);
        return 1;
    }

    if (json) {
        lr0_to_json(&c, out);
    } else if (strcmp(show, "closure") == 0) {
        for (int s = 0; s < c.state_count; s++) {
            fprintf(out, "State I%d:\n", s);
            lr0_print_set(&g, &c.states[s], out);
            fputc('\n', out);
        }
    } else if (strcmp(show, "goto") == 0) {
        fprintf(out, "Goto:\n");
        for (int e = 0; e < c.edge_count; e++) {
            fprintf(out, "  I%d --%s--> I%d\n",
                    c.edges[e].from,
                    grammar_symbol_name(&g, c.edges[e].on_symbol),
                    c.edges[e].to);
        }
    } else if (strcmp(show, "conflicts") == 0) {
        lr0_print_conflicts(&c, out);
    } else {
        lr0_print(&c, out);
    }

    if (out != stdout) fclose(out);
    return c.is_lr0 ? 0 : 0; /* 冲突不视为进程错误，由调用方按 conflicts 段判断 */
}

static int cmd_slr(int argc, char **argv) {
    const char *grammar_path = NULL;
    const char *out_path     = NULL;
    const char *show         = "all";
    bool json = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
        } else if (strncmp(argv[i], "--show=", 7) == 0) {
            show = argv[i] + 7;
        } else if (argv[i][0] != '-') {
            grammar_path = argv[i];
        }
    }

    if (!grammar_path) {
        fprintf(stderr, "usage: compiler slr <grammar_file> "
                        "[--show=first|follow|action|goto|conflicts|all] "
                        "[--format=json] [-o OUT]\n");
        return 1;
    }

    Grammar g;
    grammar_init(&g);
    if (!grammar_load(&g, grammar_path)) return 1;
    grammar_augment(&g);

    Lr0Collection *c = calloc(1, sizeof(Lr0Collection));
    SlrTable      *t = calloc(1, sizeof(SlrTable));
    if (!c || !t) {
        fprintf(stderr, "slr: out of memory\n");
        free(c); free(t);
        return 1;
    }

    lr0_init(c, &g);
    if (!lr0_build(c)) { free(c); free(t); return 1; }

    slr_init(t, &g, c);
    if (!slr_build(t))  { free(c); free(t); return 1; }

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); free(c); free(t); return 1; }
    }

    if (json) {
        slr_to_json(t, out);
    } else if (strcmp(show, "first") == 0 || strcmp(show, "follow") == 0) {
        slr_print_sets(t, out);
    } else if (strcmp(show, "action") == 0 || strcmp(show, "goto") == 0) {
        slr_print_tables(t, out);
    } else if (strcmp(show, "conflicts") == 0) {
        slr_print_conflicts(t, out);
    } else {
        slr_print(t, out);
    }

    if (out != stdout) fclose(out);
    free(c); free(t);
    return 0;
}

/* ── parse 子命令（实验五） ──────────────────────────── */

static int collect_tokens(TableScanner *ts, Token *buf, int cap) {
    int n = 0;
    Token t;
    while (ts_next(ts, &t)) {
        if (n >= cap) return n;
        buf[n++] = t;
    }
    return n;
}

/* token_name 的逆映射，用于从 JSON 字段还原 TokenKind */
static TokenKind token_kind_from_name(const char *name) {
    for (int k = TK_ERR; k <= TK_DOT; k++) {
        const char *n = token_name((TokenKind)k);
        if (n && strcmp(n, name) == 0) return (TokenKind)k;
    }
    return TK_ERR;
}

/* ── 轻量 JSON token 流解析 ──────────────────────────── */
/* 接受形如 [{"kind":"ID","lexeme":"main","line":1,"col":5}, ...] 的数组 */

typedef struct {
    const char *p;
    const char *end;
} JsonCursor;

static void json_skip_ws(JsonCursor *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' ||
                              *j->p == '\n' || *j->p == '\r' || *j->p == ',')) {
        j->p++;
    }
}

/* 把下一个 JSON 字符串解码到 buf（最长 cap-1），成功返回 true */
static bool json_parse_string(JsonCursor *j, char *buf, size_t cap) {
    json_skip_ws(j);
    if (j->p >= j->end || *j->p != '"') return false;
    j->p++;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char esc = *j->p++;
            switch (esc) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '"': case '\\': case '/': c = esc; break;
                default:  c = esc; break;
            }
        }
        if (n + 1 < cap) buf[n++] = c;
    }
    if (j->p >= j->end) return false;
    j->p++; /* 跳过尾部 " */
    buf[n] = '\0';
    return true;
}

static bool json_parse_int(JsonCursor *j, int *out) {
    json_skip_ws(j);
    if (j->p >= j->end) return false;
    bool neg = false;
    if (*j->p == '-') { neg = true; j->p++; }
    if (j->p >= j->end || *j->p < '0' || *j->p > '9') return false;
    int v = 0;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        v = v * 10 + (*j->p - '0');
        j->p++;
    }
    *out = neg ? -v : v;
    return true;
}

/* 期望紧接着出现指定字符；遇到则消费并返回 true */
static bool json_expect(JsonCursor *j, char ch) {
    json_skip_ws(j);
    if (j->p < j->end && *j->p == ch) { j->p++; return true; }
    return false;
}

/* 从 JSON 文件加载 token 数组到 buf。返回读入的 token 数；
 * 出错路径把诊断写入 errs（LEX 类）后返回 -1。 */
static int load_tokens_from_json(const char *path, Token *buf, int cap, ErrList *errs) {
    char *text = read_file(path);
    if (!text) {
        err_list_push(errs, ERR_LEX, 0, 0, "cannot open token file '%s'", path);
        return -1;
    }
    JsonCursor jc = { text, text + strlen(text) };
    if (!json_expect(&jc, '[')) {
        err_list_push(errs, ERR_LEX, 0, 0, "token JSON must start with '['");
        free(text);
        return -1;
    }
    int n = 0;
    while (1) {
        json_skip_ws(&jc);
        if (jc.p < jc.end && *jc.p == ']') { jc.p++; break; }
        if (n >= cap) {
            err_list_push(errs, ERR_LEX, 0, 0,
                          "token stream too long (max %d)", cap);
            free(text);
            return -1;
        }
        if (!json_expect(&jc, '{')) {
            err_list_push(errs, ERR_LEX, 0, 0, "expected '{' at token index %d", n);
            free(text);
            return -1;
        }
        Token t = (Token){0};
        bool have_kind = false;
        while (1) {
            json_skip_ws(&jc);
            if (jc.p < jc.end && *jc.p == '}') { jc.p++; break; }
            char key[32];
            if (!json_parse_string(&jc, key, sizeof(key))) {
                err_list_push(errs, ERR_LEX, 0, 0,
                              "expected JSON key at token %d", n);
                free(text);
                return -1;
            }
            if (!json_expect(&jc, ':')) {
                err_list_push(errs, ERR_LEX, 0, 0,
                              "expected ':' after key '%s' (token %d)", key, n);
                free(text);
                return -1;
            }
            if (strcmp(key, "kind") == 0) {
                char val[32];
                if (!json_parse_string(&jc, val, sizeof(val))) {
                    err_list_push(errs, ERR_LEX, 0, 0,
                                  "kind must be a string (token %d)", n);
                    free(text);
                    return -1;
                }
                TokenKind k = token_kind_from_name(val);
                if (k == TK_ERR && strcmp(val, "ERR") != 0) {
                    err_list_push(errs, ERR_LEX, 0, 0,
                                  "unknown token kind '%s' (token %d)", val, n);
                }
                t.kind = k;
                have_kind = true;
            } else if (strcmp(key, "lexeme") == 0) {
                json_parse_string(&jc, t.lexeme, sizeof(t.lexeme));
            } else if (strcmp(key, "line") == 0) {
                json_parse_int(&jc, &t.line);
            } else if (strcmp(key, "col") == 0) {
                json_parse_int(&jc, &t.col);
            } else {
                /* 未知字段：吞掉它的值 */
                json_skip_ws(&jc);
                if (jc.p < jc.end && *jc.p == '"') {
                    char dummy[256];
                    json_parse_string(&jc, dummy, sizeof(dummy));
                } else {
                    int v;
                    json_parse_int(&jc, &v);
                }
            }
        }
        if (!have_kind) {
            err_list_push(errs, ERR_LEX, t.line, t.col,
                          "token %d missing 'kind' field", n);
        }
        buf[n++] = t;
    }
    free(text);
    return n;
}

static int cmd_parse(int argc, char **argv) {
    const char *in_path = NULL;
    const char *tokens_path = NULL;
    const char *out_path = NULL;
    const char *grammar_path = "data/c_lite.grammar";
    const char *dfa_path = DEFAULT_LEXER_DFA;
    bool json = false;
    bool trace_json = false;
    const char *show = "all";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) {
            tokens_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--grammar") == 0 && i + 1 < argc) {
            grammar_path = argv[++i];
        } else if (strcmp(argv[i], "--dfa") == 0 && i + 1 < argc) {
            dfa_path = argv[++i];
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
        } else if (strcmp(argv[i], "--trace=json") == 0) {
            json = true;
            trace_json = true;
        } else if (strncmp(argv[i], "--show=", 7) == 0) {
            show = argv[i] + 7;
        }
    }

    if (!in_path && !tokens_path) {
        fprintf(stderr,
            "usage: compiler parse [-f IN | --tokens FILE.json] [-o OUT] [--grammar PATH] [--dfa PATH]\n"
            "                      [--show=ast|symtab|errors|all] [--format=json] [--trace=json]\n");
        return 1;
    }
    if (in_path && tokens_path) {
        fprintf(stderr, "compiler parse: -f and --tokens are mutually exclusive\n");
        return 1;
    }

    /* Token 流由两种途径之一产生：内部 lexer 或外部 JSON 文件 */
    DFA dfa;
    char *src = NULL;
    AstArena arena;
    SymTab   symtab;
    ErrList  errors;
    arena_init(&arena);
    symtab_init(&symtab);
    err_list_init(&errors);
    static Token toks[PARSER_TOKEN_CAP];
    int n_tok = 0;

    if (in_path) {
        if (!load_table_dfa(&dfa, dfa_path)) return 1;
        src = read_file(in_path);
        if (!src) return 1;
        TableScanner ts;
        ts_init(&ts, &dfa, src);
        n_tok = collect_tokens(&ts, toks, PARSER_TOKEN_CAP);
    } else {
        int n = load_tokens_from_json(tokens_path, toks, PARSER_TOKEN_CAP, &errors);
        if (n < 0) n_tok = 0;
        else       n_tok = n;
    }

    Grammar g;
    grammar_init(&g);
    if (!grammar_load(&g, grammar_path)) { free(src); return 1; }
    grammar_augment(&g);

    Lr0Collection *lc = calloc(1, sizeof(Lr0Collection));
    SlrTable      *st = calloc(1, sizeof(SlrTable));
    if (!lc || !st) { free(lc); free(st); free(src); return 1; }
    lr0_init(lc, &g);
    if (!lr0_build(lc)) { free(lc); free(st); free(src); return 1; }
    slr_init(st, &g, lc);
    slr_build(st);

    /* lexer 的 ERR token 已经在 ts.error_count 里统计；同步进 errors */
    for (int i = 0; i < n_tok; i++) {
        if (toks[i].kind == TK_ERR) {
            err_list_push(&errors, ERR_LEX, toks[i].line, toks[i].col,
                          "unrecognized lexeme '%s'", toks[i].lexeme);
        }
    }

    Parser p;
    parser_init(&p, &arena, &symtab, &errors, &g, st);
    ParseTrace *trace = trace_json ? parse_trace_new() : NULL;
    p.trace = trace;
    AstNode *root = parser_run(&p, toks, n_tok);
    if (root) semantic_check(root, &symtab, &errors);
    bool accepted = (errors.count == 0);

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); free(lc); free(st); free(src); return 1; }
    }

    if (json) {
        fprintf(out, "{\n  \"accepted\": %s,\n", accepted ? "true" : "false");
        fprintf(out, "  \"tokens\": [");
        for (int i = 0; i < n_tok; i++) {
            if (i) fputc(',', out);
            fprintf(out, "{\"kind\":\"%s\",\"lexeme\":\"%s\",\"line\":%d,\"col\":%d}",
                    token_name(toks[i].kind), toks[i].lexeme, toks[i].line, toks[i].col);
        }
        fprintf(out, "],\n  \"ast\": ");
        ast_to_json(root, out);
        fprintf(out, ",\n  \"symtab\": ");
        symtab_to_json(&symtab, out);
        fprintf(out, ",\n  \"errors\": ");
        err_list_to_json(&errors, out);
        if (trace_json) {
            fprintf(out, ",\n  \"productions\": [");
            for (int i = 0; i < g.production_count; i++) {
                if (i) fputc(',', out);
                const GrProduction *pr = &g.productions[i];
                fprintf(out, "{\"id\":%d,\"lhs\":\"%s\",\"rhs\":[",
                        i, grammar_symbol_name(&g, pr->lhs));
                for (int j = 0; j < pr->rhs_len; j++) {
                    if (j) fputc(',', out);
                    fprintf(out, "\"%s\"", grammar_symbol_name(&g, pr->rhs[j]));
                }
                fputs("]}", out);
            }
            fputc(']', out);
            fprintf(out, ",\n  \"symbols\": [");
            for (int i = 0; i < g.symbol_count; i++) {
                if (i) fputc(',', out);
                fprintf(out, "{\"id\":%d,\"name\":\"%s\"}", i,
                        grammar_symbol_name(&g, i));
            }
            fputc(']', out);
            fprintf(out, ",\n  \"steps\": ");
            parse_trace_to_json(p.trace, out);
        }
        fprintf(out, "\n}\n");
    } else {
        if (strcmp(show, "errors") == 0 || strcmp(show, "all") == 0) {
            err_list_print(&errors, out);
            fprintf(out, "\n");
        }
        if (strcmp(show, "symtab") == 0 || strcmp(show, "all") == 0) {
            symtab_print(&symtab, out);
            fprintf(out, "\n");
        }
        if (strcmp(show, "all") == 0) {
            fprintf(out, "Status: %s\n", accepted ? "ACCEPTED" : "FAILED");
        }
    }

    if (out != stdout) fclose(out);
    arena_free(&arena);
    parse_trace_free(trace);
    free(lc); free(st); free(src);
    return accepted ? 0 : 1;
}

static int cmd_ir(int argc, char **argv) {
    const char *in_path = NULL;
    const char *tokens_path = NULL;
    const char *out_path = NULL;
    const char *grammar_path = "data/c_lite.grammar";
    const char *dfa_path = DEFAULT_LEXER_DFA;
    bool json = false;
    const char *show = "quads";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) {
            tokens_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--grammar") == 0 && i + 1 < argc) {
            grammar_path = argv[++i];
        } else if (strcmp(argv[i], "--dfa") == 0 && i + 1 < argc) {
            dfa_path = argv[++i];
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
        } else if (strncmp(argv[i], "--show=", 7) == 0) {
            show = argv[i] + 7;
        }
    }

    if (!in_path && !tokens_path) {
        fprintf(stderr,
            "usage: compiler ir [-f IN | --tokens FILE.json] [-o OUT] [--grammar PATH] [--dfa PATH]\n"
            "                   [--show=quads|symtab|errors|all] [--format=json]\n");
        return 1;
    }
    if (in_path && tokens_path) {
        fprintf(stderr, "compiler ir: -f and --tokens are mutually exclusive\n");
        return 1;
    }

    /* —— parse 阶段（前置） —— */
    DFA dfa;
    char *src = NULL;
    AstArena arena;
    SymTab   symtab;
    ErrList  errors;
    arena_init(&arena);
    symtab_init(&symtab);
    err_list_init(&errors);
    static Token toks[PARSER_TOKEN_CAP];
    int n_tok = 0;

    if (in_path) {
        if (!load_table_dfa(&dfa, dfa_path)) return 1;
        src = read_file(in_path);
        if (!src) return 1;
        TableScanner ts;
        ts_init(&ts, &dfa, src);
        n_tok = collect_tokens(&ts, toks, PARSER_TOKEN_CAP);
    } else {
        int n = load_tokens_from_json(tokens_path, toks, PARSER_TOKEN_CAP, &errors);
        n_tok = (n < 0) ? 0 : n;
    }

    Grammar g;
    grammar_init(&g);
    if (!grammar_load(&g, grammar_path)) { free(src); return 1; }
    grammar_augment(&g);

    Lr0Collection *lc = calloc(1, sizeof(Lr0Collection));
    SlrTable      *st = calloc(1, sizeof(SlrTable));
    if (!lc || !st) { free(lc); free(st); free(src); return 1; }
    lr0_init(lc, &g);
    if (!lr0_build(lc)) { free(lc); free(st); free(src); return 1; }
    slr_init(st, &g, lc);
    slr_build(st);

    for (int i = 0; i < n_tok; i++) {
        if (toks[i].kind == TK_ERR) {
            err_list_push(&errors, ERR_LEX, toks[i].line, toks[i].col,
                          "unrecognized lexeme '%s'", toks[i].lexeme);
        }
    }

    Parser p;
    parser_init(&p, &arena, &symtab, &errors, &g, st);
    p.trace = NULL;
    AstNode *root = parser_run(&p, toks, n_tok);
    if (root) semantic_check(root, &symtab, &errors);
    bool accepted = (errors.count == 0);

    /* —— IR 生成 —— */
    QuadList quads;
    quad_list_init(&quads);
    if (root) ir_generate(&quads, root, &symtab);

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); free(lc); free(st); free(src); return 1; }
    }

    if (json) {
        fprintf(out, "{\n  \"accepted\": %s,\n", accepted ? "true" : "false");
        fprintf(out, "  \"tokens\": [");
        for (int i = 0; i < n_tok; i++) {
            if (i) fputc(',', out);
            fprintf(out, "{\"kind\":\"%s\",\"lexeme\":\"%s\",\"line\":%d,\"col\":%d}",
                    token_name(toks[i].kind), toks[i].lexeme, toks[i].line, toks[i].col);
        }
        fprintf(out, "],\n  \"ast\": ");
        ast_to_json(root, out);
        fprintf(out, ",\n  \"symtab\": ");
        symtab_to_json(&symtab, out);
        fprintf(out, ",\n  \"errors\": ");
        err_list_to_json(&errors, out);
        fprintf(out, ",\n  \"quads\": ");
        ir_to_json(&quads, out);
        fprintf(out, ",\n  \"temp_count\": %d,\n  \"label_count\": %d\n}\n",
                quads.next_temp, quads.next_label);
    } else {
        if (strcmp(show, "errors") == 0 || strcmp(show, "all") == 0) {
            err_list_print(&errors, out);
            fprintf(out, "\n");
        }
        if (strcmp(show, "symtab") == 0 || strcmp(show, "all") == 0) {
            symtab_print(&symtab, out);
            fprintf(out, "\n");
        }
        if (strcmp(show, "quads") == 0 || strcmp(show, "all") == 0) {
            fprintf(out, "Quadruples (%d total):\n", quads.count);
            ir_print(&quads, out);
            fprintf(out, "\n");
        }
        if (strcmp(show, "all") == 0) {
            fprintf(out, "Status: %s\n", accepted ? "ACCEPTED" : "FAILED");
        }
    }

    if (out != stdout) fclose(out);
    arena_free(&arena);
    quad_list_free(&quads);
    free(lc); free(st); free(src);
    return accepted ? 0 : 1;
}

/* ── cmd_codegen: 共用 parse → ir → memmap → (asm) → 可选 exec 的流水线 ─── */

typedef enum { CG_MEMMAP, CG_ASM, CG_EXEC } CodegenMode;

static int cmd_codegen(int argc, char **argv, CodegenMode mode) {
    const char *in_path = NULL, *tokens_path = NULL, *out_path = NULL;
    const char *grammar_path = "data/c_lite.grammar";
    const char *dfa_path = DEFAULT_LEXER_DFA;
    const char *stdin_arg = NULL;
    bool json = false;
    const char *show = (mode == CG_MEMMAP) ? "all" : "asm";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) in_path = argv[++i];
        else if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) tokens_path = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--grammar") == 0 && i + 1 < argc) grammar_path = argv[++i];
        else if (strcmp(argv[i], "--dfa") == 0 && i + 1 < argc) dfa_path = argv[++i];
        else if (strcmp(argv[i], "--stdin") == 0 && i + 1 < argc) stdin_arg = argv[++i];
        else if (strcmp(argv[i], "--format=json") == 0) json = true;
        else if (strncmp(argv[i], "--show=", 7) == 0) show = argv[i] + 7;
    }

    if (!in_path && !tokens_path) {
        const char *cmd = (mode == CG_MEMMAP) ? "memmap" : (mode == CG_ASM ? "asm" : "exec");
        fprintf(stderr,
            "usage: compiler %s [-f IN | --tokens FILE.json] [-o OUT] [--grammar PATH] [--dfa PATH]\n"
            "                  [--show=...] [--format=json]%s\n",
            cmd, mode == CG_EXEC ? " [--stdin STR]" : "");
        return 1;
    }
    if (in_path && tokens_path) {
        fprintf(stderr, "-f and --tokens are mutually exclusive\n");
        return 1;
    }

    DFA dfa;
    char *src = NULL;
    AstArena arena;  SymTab symtab;  ErrList errors;
    arena_init(&arena); symtab_init(&symtab); err_list_init(&errors);
    static Token toks[PARSER_TOKEN_CAP];
    int n_tok = 0;

    if (in_path) {
        if (!load_table_dfa(&dfa, dfa_path)) return 1;
        src = read_file(in_path);
        if (!src) return 1;
        TableScanner ts;  ts_init(&ts, &dfa, src);
        n_tok = collect_tokens(&ts, toks, PARSER_TOKEN_CAP);
    } else {
        int n = load_tokens_from_json(tokens_path, toks, PARSER_TOKEN_CAP, &errors);
        n_tok = (n < 0) ? 0 : n;
    }

    Grammar g;  grammar_init(&g);
    if (!grammar_load(&g, grammar_path)) { free(src); return 1; }
    grammar_augment(&g);

    Lr0Collection *lc = calloc(1, sizeof(Lr0Collection));
    SlrTable *st = calloc(1, sizeof(SlrTable));
    if (!lc || !st) { free(lc); free(st); free(src); return 1; }
    lr0_init(lc, &g);
    if (!lr0_build(lc)) { free(lc); free(st); free(src); return 1; }
    slr_init(st, &g, lc);  slr_build(st);

    for (int i = 0; i < n_tok; i++) {
        if (toks[i].kind == TK_ERR) {
            err_list_push(&errors, ERR_LEX, toks[i].line, toks[i].col,
                          "unrecognized lexeme '%s'", toks[i].lexeme);
        }
    }

    Parser p; parser_init(&p, &arena, &symtab, &errors, &g, st);
    p.trace = NULL;
    AstNode *root = parser_run(&p, toks, n_tok);
    if (root) semantic_check(root, &symtab, &errors);
    bool accepted = (errors.count == 0);

    QuadList quads;  quad_list_init(&quads);
    if (root && accepted) ir_generate(&quads, root, &symtab);

    MemMap mm;  memmap_build(&mm, &quads, &symtab);

    /* asm 文本：先写到 tmpfile，再读回 buf（跨平台） */
    char *asm_buf = NULL;  size_t asm_len = 0;
    if (mode == CG_ASM || mode == CG_EXEC) {
        FILE *tf = tmpfile();
        if (tf) {
            asm_arm64_emit(&quads, &mm, tf);
            fflush(tf);
            long sz = ftell(tf);
            if (sz > 0) {
                rewind(tf);
                asm_buf = malloc((size_t)sz + 1);
                if (asm_buf) {
                    asm_len = fread(asm_buf, 1, (size_t)sz, tf);
                    asm_buf[asm_len] = '\0';
                }
            }
            fclose(tf);
        }
    }

    /* CG_EXEC：写 .s 到临时文件，gcc 编译，运行（仅 POSIX 平台） */
    char prog_stdout[8192] = "";
    char compile_stderr[2048] = "";
    int exit_code = -1;
#ifndef _WIN32
    if (mode == CG_EXEC && accepted && asm_buf) {
        char tmpl_s[] = "/tmp/compiler_asm_XXXXXX.s";
        char tmpl_e[] = "/tmp/compiler_exe_XXXXXX";
        int fd_s = mkstemps(tmpl_s, 2);
        int fd_e = mkstemp(tmpl_e);
        if (fd_s >= 0) { (void)!write(fd_s, asm_buf, asm_len); close(fd_s); }
        if (fd_e >= 0) close(fd_e);

        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "gcc -o %s %s 2>&1", tmpl_e, tmpl_s);
        FILE *gp = popen(cmd, "r");
        if (gp) {
            fread(compile_stderr, 1, sizeof(compile_stderr) - 1, gp);
            int gc = pclose(gp);
            if (gc == 0) {
                char run_cmd[1024];
                if (stdin_arg && *stdin_arg) {
                    snprintf(run_cmd, sizeof(run_cmd),
                             "printf %%s '%s' | timeout 5 %s 2>&1", stdin_arg, tmpl_e);
                } else {
                    snprintf(run_cmd, sizeof(run_cmd),
                             "timeout 5 %s </dev/null 2>&1", tmpl_e);
                }
                FILE *rp = popen(run_cmd, "r");
                if (rp) {
                    fread(prog_stdout, 1, sizeof(prog_stdout) - 1, rp);
                    int rc = pclose(rp);
                    exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);
                }
            } else {
                exit_code = -2;
            }
        }
        unlink(tmpl_s); unlink(tmpl_e);
    }
#else
    if (mode == CG_EXEC) {
        snprintf(compile_stderr, sizeof(compile_stderr),
                 "exec mode is POSIX-only; use server-side compiler exec.\n");
    }
#endif
    (void)stdin_arg;

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); free(lc); free(st); free(src); free(asm_buf); return 1; }
    }

    if (json) {
        fprintf(out, "{\n  \"accepted\": %s,\n", accepted ? "true" : "false");
        fprintf(out, "  \"errors\": "); err_list_to_json(&errors, out);
        fprintf(out, ",\n  \"symtab\": "); symtab_to_json(&symtab, out);
        fprintf(out, ",\n  \"quads\": "); ir_to_json(&quads, out);
        fprintf(out, ",\n  \"memmap\": "); memmap_to_json(&mm, out);
        if (asm_buf) {
            fprintf(out, ",\n  \"asm\": ");
            fputc('"', out);
            for (size_t i = 0; i < asm_len; i++) {
                char c = asm_buf[i];
                if (c == '"') fputs("\\\"", out);
                else if (c == '\\') fputs("\\\\", out);
                else if (c == '\n') fputs("\\n", out);
                else if (c == '\t') fputs("\\t", out);
                else if (c == '\r') fputs("\\r", out);
                else fputc(c, out);
            }
            fputc('"', out);
        }
        if (mode == CG_EXEC) {
            fprintf(out, ",\n  \"compile_stderr\": ");
            fputc('"', out);
            for (char *p2 = compile_stderr; *p2; p2++) {
                if (*p2 == '"') fputs("\\\"", out);
                else if (*p2 == '\\') fputs("\\\\", out);
                else if (*p2 == '\n') fputs("\\n", out);
                else fputc(*p2, out);
            }
            fputc('"', out);
            fprintf(out, ",\n  \"program_stdout\": ");
            fputc('"', out);
            for (char *p2 = prog_stdout; *p2; p2++) {
                if (*p2 == '"') fputs("\\\"", out);
                else if (*p2 == '\\') fputs("\\\\", out);
                else if (*p2 == '\n') fputs("\\n", out);
                else fputc(*p2, out);
            }
            fputc('"', out);
            fprintf(out, ",\n  \"exit_code\": %d", exit_code);
        }
        fprintf(out, "\n}\n");
    } else {
        if (strcmp(show, "errors") == 0 || strcmp(show, "all") == 0) {
            err_list_print(&errors, out); fputc('\n', out);
        }
        if (mode == CG_MEMMAP &&
            (strcmp(show, "memmap") == 0 || strcmp(show, "all") == 0)) {
            memmap_print(&mm, out); fputc('\n', out);
        }
        if ((mode == CG_ASM || mode == CG_EXEC) &&
            (strcmp(show, "asm") == 0 || strcmp(show, "all") == 0) && asm_buf) {
            fwrite(asm_buf, 1, asm_len, out);
        }
        if (mode == CG_EXEC) {
            fprintf(out, "\n--- compile stderr ---\n%s", compile_stderr);
            fprintf(out, "--- program stdout ---\n%s", prog_stdout);
            fprintf(out, "--- exit code: %d ---\n", exit_code);
        }
        if (strcmp(show, "all") == 0)
            fprintf(out, "Status: %s\n", accepted ? "ACCEPTED" : "FAILED");
    }

    if (out != stdout) fclose(out);
    arena_free(&arena);
    quad_list_free(&quads);
    free(asm_buf);
    free(lc); free(st); free(src);
    return accepted ? 0 : 1;
}

static int cmd_memmap(int argc, char **argv) { return cmd_codegen(argc, argv, CG_MEMMAP); }
static int cmd_asm   (int argc, char **argv) { return cmd_codegen(argc, argv, CG_ASM); }
static int cmd_exec  (int argc, char **argv) { return cmd_codegen(argc, argv, CG_EXEC); }

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s dfa <file.dfa> [--enumerate N] [--test STR] [--trace] [--format=json]\n"
        "  %s scan [-f IN [-o OUT]] [--impl=table|--impl=hand] [--table DFA] [--compare] [--format=json]\n"
        "  %s lr0 <file.grammar> [--show=closure|goto|conflicts|productions|all] [--format=json] [-o OUT]\n"
        "  %s slr <file.grammar> [--show=first|follow|action|goto|conflicts|all] [--format=json] [-o OUT]\n"
        "  %s parse [-f IN | --tokens FILE.json] [-o OUT] [--grammar PATH] [--dfa PATH]\n"
        "                  [--show=...] [--format=json] [--trace=json]\n"
        "  %s ir [-f IN | --tokens FILE.json] [-o OUT] [--grammar PATH] [--dfa PATH]\n"
        "                  [--show=quads|symtab|errors|all] [--format=json]\n"
        "  %s [--stage=scan] [-f IN [-o OUT]]   (legacy mode)\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    if (strcmp(argv[1], "dfa") == 0) return cmd_dfa(argc - 2, argv + 2);
    if (strcmp(argv[1], "scan") == 0) return cmd_scan(argc - 2, argv + 2);
    if (strcmp(argv[1], "lr0") == 0) return cmd_lr0(argc - 2, argv + 2);
    if (strcmp(argv[1], "slr") == 0) return cmd_slr(argc - 2, argv + 2);
    if (strcmp(argv[1], "parse") == 0) return cmd_parse(argc - 2, argv + 2);
    if (strcmp(argv[1], "ir") == 0) return cmd_ir(argc - 2, argv + 2);
    if (strcmp(argv[1], "memmap") == 0) return cmd_memmap(argc - 2, argv + 2);
    if (strcmp(argv[1], "asm")    == 0) return cmd_asm   (argc - 2, argv + 2);
    if (strcmp(argv[1], "exec")   == 0) return cmd_exec  (argc - 2, argv + 2);

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--stage=", 8) == 0) {
            const char *stage = argv[i] + 8;
            if (strcmp(stage, "scan") != 0) {
                fprintf(stderr, "unknown stage: %s\n", stage);
                return 1;
            }
        }
    }

    int sub_argc = 0;
    char *sub_argv[32];
    for (int i = 1; i < argc && sub_argc < 32; i++) {
        if (strncmp(argv[i], "--stage=", 8) != 0)
            sub_argv[sub_argc++] = argv[i];
    }
    return cmd_scan(sub_argc, sub_argv);
}
