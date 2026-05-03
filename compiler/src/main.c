#include "token.h"
#include "scanner.h"
#include "table_scanner.h"
#include "dfa.h"

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

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s dfa <file.dfa> [--enumerate N] [--test STR] [--trace] [--format=json]\n"
        "  %s scan [-f IN [-o OUT]] [--impl=table|--impl=hand] [--table DFA] [--compare] [--format=json]\n"
        "  %s [--stage=scan] [-f IN [-o OUT]]   (legacy mode)\n",
        argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    if (strcmp(argv[1], "dfa") == 0) return cmd_dfa(argc - 2, argv + 2);
    if (strcmp(argv[1], "scan") == 0) return cmd_scan(argc - 2, argv + 2);

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
