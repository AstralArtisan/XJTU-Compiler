#include "table_scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    if (out_len) *out_len = got;
    return buf;
}

static int run_mode_1(const TsTable *t) {
    int n = 0;
    if (scanf("%d", &n) != 1) return 1;
    for (int i = 0; i < n; ++i) {
        char buf[TS_LEXEME_LIMIT];
        if (scanf("%127s", buf) != 1) return 1;
        TsScanner s; ts_init(&s, buf);
        TsToken tok;
        if (ts_next(t, &s, &tok)) printf("%s\n", tok.token);
        else printf("ERR\n");
    }
    return 0;
}

static int run_mode_2(const TsTable *t) {
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
    TsScanner s; ts_init(&s, line);
    TsToken tok;
    while (ts_next(t, &s, &tok)) printf("%s\n", tok.token);
    return 0;
}

static int run_mode_3(const TsTable *t, const char *in_path, const char *out_path) {
    size_t len;
    char *src = read_file_all(in_path, &len);
    if (!src) return 1;
    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror(out_path); free(src); return 1; }
    }
    TsScanner s; ts_init(&s, src);
    TsToken tok;
    int count = 0;
    while (ts_next(t, &s, &tok)) {
        fprintf(out, "(%s, %s) @%d:%d\n", tok.token, tok.lexeme, tok.line, tok.col);
        count++;
    }
    fprintf(stderr, "[scanner_table] %d tokens, %d errors\n", count, s.errors);
    if (out != stdout) fclose(out);
    free(src);
    return s.errors == 0 ? 0 : 2;
}

static int run_dump(const TsTable *t) {
    ts_dump(t);
    return 0;
}

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s --dfa FILE             # interactive mode 1/2 with table from FILE\n"
        "  %s --dfa FILE -f IN [-o OUT]  # mode 3: scan file IN with table\n"
        "  %s --dfa FILE --dump      # dump loaded table summary\n",
        argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    const char *dfa_path = NULL;
    const char *in_path = NULL;
    const char *out_path = NULL;
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dfa") == 0 && i + 1 < argc) dfa_path = argv[++i];
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) in_path = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0) dump = true;
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); print_usage(argv[0]); return 1; }
    }
    if (!dfa_path) { print_usage(argv[0]); return 1; }

    TsTable t;
    char err[256];
    if (!ts_load(&t, dfa_path, err, sizeof(err))) {
        fprintf(stderr, "load failed: %s\n", err);
        return 1;
    }

    if (dump) return run_dump(&t);
    if (in_path) return run_mode_3(&t, in_path, out_path);

    int mode = 0;
    if (scanf("%d", &mode) != 1) { print_usage(argv[0]); return 1; }
    if (mode == 1) return run_mode_1(&t);
    if (mode == 2) return run_mode_2(&t);
    fprintf(stderr, "unknown mode: %d\n", mode);
    return 1;
}
