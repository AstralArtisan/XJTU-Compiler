#include "token.h"
#include "scanner.h"
#include "table_scanner.h"
#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── utilities ───────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* ── scan subcommand ─────────────────────────────────── */

static int scan_mode_1(void) {
    int n = 0;
    if (scanf("%d", &n) != 1) return 1;
    for (int i = 0; i < n; i++) {
        char buf[TOKEN_LEXEME_LIMIT];
        if (scanf("%127s", buf) != 1) return 1;
        Scanner s;
        scanner_init(&s, buf);
        Token t;
        if (scanner_next(&s, &t) && t.kind != TK_EOF)
            printf("%s\n", token_name(t.kind));
        else
            printf("ERR\n");
    }
    return 0;
}

static int scan_mode_2(void) {
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
    Scanner s;
    scanner_init(&s, line);
    Token t;
    while (scanner_next(&s, &t))
        printf("%s\n", token_name(t.kind));
    return 0;
}

static void scan_file_text(Scanner *s, FILE *out) {
    Token t;
    int count = 0;
    while (scanner_next(s, &t)) {
        fprintf(out, "(%s, %s) @%d:%d\n", token_name(t.kind), t.lexeme, t.line, t.col);
        count++;
    }
    fprintf(stderr, "[scanner] %d tokens, %d errors\n", count, s->error_count);
}

static void scan_file_json(Scanner *s, FILE *out) {
    Token t;
    fprintf(out, "[\n");
    bool first = true;
    while (scanner_next(s, &t)) {
        if (!first) fprintf(out, ",\n");
        fprintf(out, "  {\"kind\":\"%s\",\"lexeme\":\"%s\",\"line\":%d,\"col\":%d}",
                token_name(t.kind), t.lexeme, t.line, t.col);
        first = false;
    }
    fprintf(out, "\n]\n");
}

static void ts_file_text(TableScanner *ts, FILE *out) {
    Token t;
    int count = 0;
    while (ts_next(ts, &t)) {
        fprintf(out, "(%s, %s) @%d:%d\n", token_name(t.kind), t.lexeme, t.line, t.col);
        count++;
    }
    fprintf(stderr, "[table-scanner] %d tokens, %d errors\n", count, ts->error_count);
}

static void ts_file_json(TableScanner *ts, FILE *out) {
    Token t;
    fprintf(out, "[\n");
    bool first = true;
    while (ts_next(ts, &t)) {
        if (!first) fprintf(out, ",\n");
        fprintf(out, "  {\"kind\":\"%s\",\"lexeme\":\"%s\",\"line\":%d,\"col\":%d}",
                token_name(t.kind), t.lexeme, t.line, t.col);
        first = false;
    }
    fprintf(out, "\n]\n");
}

static int cmd_scan(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    const char *table_path = NULL;
    bool json = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
            in_path = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_path = argv[++i];
        else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc)
            table_path = argv[++i];
        else if (strcmp(argv[i], "--format=json") == 0)
            json = true;
    }

    if (in_path) {
        char *src = read_file(in_path);
        if (!src) return 1;
        FILE *out = stdout;
        if (out_path) {
            out = fopen(out_path, "w");
            if (!out) { perror(out_path); free(src); return 1; }
        }

        if (table_path) {
            DFA dfa;
            dfa_init(&dfa);
            if (!dfa_load(&dfa, table_path) || !dfa_validate(&dfa)) {
                free(src);
                if (out != stdout) fclose(out);
                return 1;
            }
            TableScanner ts;
            ts_init(&ts, &dfa, src);
            if (json) ts_file_json(&ts, out);
            else      ts_file_text(&ts, out);
        } else {
            Scanner s;
            scanner_init(&s, src);
            if (json) scan_file_json(&s, out);
            else      scan_file_text(&s, out);
        }

        if (out != stdout) fclose(out);
        free(src);
        return 0;
    }

    int mode = 0;
    if (scanf("%d", &mode) != 1) return 1;
    if (mode == 1) return scan_mode_1();
    if (mode == 2) return scan_mode_2();
    fprintf(stderr, "unknown mode: %d\n", mode);
    return 1;
}

/* ── dfa subcommand ──────────────────────────────────── */

static int cmd_dfa(int argc, char **argv) {
    const char *dfa_path = NULL;
    int enumerate_len = -1;
    const char *test_str = NULL;
    bool json = false;
    bool interactive = true;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--enumerate") == 0 && i + 1 < argc) {
            enumerate_len = atoi(argv[++i]);
            interactive = false;
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            test_str = argv[++i];
            interactive = false;
        } else if (strcmp(argv[i], "--format=json") == 0) {
            json = true;
            interactive = false;
        } else if (argv[i][0] != '-') {
            dfa_path = argv[i];
        }
    }

    if (!dfa_path) {
        fprintf(stderr, "usage: compiler dfa <file.dfa> [--enumerate N] [--test STR] [--format=json]\n");
        return 1;
    }

    DFA dfa;
    dfa_init(&dfa);
    if (!dfa_load(&dfa, dfa_path)) return 1;
    if (!dfa_validate(&dfa)) return 1;

    if (json) { dfa_to_json(&dfa, stdout); return 0; }

    if (enumerate_len >= 0) { dfa_enumerate(&dfa, enumerate_len, stdout); return 0; }

    if (test_str) {
        bool ok = dfa_simulate(&dfa, test_str);
        printf("%s: %s\n", test_str, ok ? "ACCEPT" : "REJECT");
        return ok ? 0 : 1;
    }

    if (interactive) {
        dfa_print(&dfa, stdout);
        printf("\nMax length to enumerate: ");
        int n;
        if (scanf("%d", &n) == 1)
            dfa_enumerate(&dfa, n, stdout);
        printf("\nEnter string to test (empty line to quit): ");
        char buf[256];
        while (scanf("%255s", buf) == 1) {
            printf("%s: %s\n", buf, dfa_simulate(&dfa, buf) ? "ACCEPT" : "REJECT");
            printf("Enter string to test (empty line to quit): ");
        }
    }
    return 0;
}

/* ── main: subcommand dispatch ───────────────────────── */

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s dfa <file.dfa> [--enumerate N] [--test STR] [--format=json]\n"
        "  %s scan [-f IN [-o OUT]] [--format=json]\n"
        "  %s [--stage=scan] [-f IN [-o OUT]]   (legacy mode)\n",
        argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    if (strcmp(argv[1], "dfa") == 0)
        return cmd_dfa(argc - 2, argv + 2);

    if (strcmp(argv[1], "scan") == 0)
        return cmd_scan(argc - 2, argv + 2);

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    /* legacy: --stage=scan or direct -f */
    bool has_stage = false;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--stage=", 8) == 0) {
            const char *stage = argv[i] + 8;
            if (strcmp(stage, "scan") != 0) {
                fprintf(stderr, "unknown stage: %s\n", stage);
                return 1;
            }
            has_stage = true;
        }
    }
    (void)has_stage;

    int sub_argc = 0;
    char *sub_argv[16];
    for (int i = 1; i < argc && sub_argc < 16; i++) {
        if (strncmp(argv[i], "--stage=", 8) != 0)
            sub_argv[sub_argc++] = argv[i];
    }
    return cmd_scan(sub_argc, sub_argv);
}
