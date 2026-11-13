#include "sysprobe.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out) {
    fputs("SysProbe v2.0 — Linux Task Manager\n"
          "Usage: sysprobe [OPTIONS]\n\n"
          "  --tui               Launch live terminal dashboard (interactive TTY)\n"
          "  --json              Output machine-readable JSON\n"
          "  --processes         Show top 10 processes sorted by RSS\n"
          "  --top N             Show N processes (1-2048); implies --processes\n"
          "  --pid PID           Inspect one accessible PID; implies --processes\n"
          "  --interval MS       CPU sampling interval in ms (100-5000; default 250)\n"
          "  --version           Print version and exit\n"
          "  --help              Print help and exit\n\n"
          "System stats are sampled once. CPU TICKS are cumulative, not process CPU %.\n", out);
}

static int positive_number(const char *text, long max, long *out) {
    if (!text || !*text || *text < '0' || *text > '9') return -1;
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno || *end || value < 1 || value > max) return -1;
    *out = value;
    return 0;
}

int main(int argc, char **argv) {
    SpOptions opts = {.interval_ms = 250, .top = 10, .pid = 0, .json = 0, .processes = 0};
    int tui = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(stdout); return 0; }
        if (!strcmp(argv[i], "--version")) { puts("SysProbe 2.0.0"); return 0; }
        if (!strcmp(argv[i], "--tui")) { tui = 1; continue; }
        if (!strcmp(argv[i], "--json")) { opts.json = 1; continue; }
        if (!strcmp(argv[i], "--processes")) { opts.processes = 1; continue; }
        if (!strcmp(argv[i], "--top") || !strcmp(argv[i], "--pid") || !strcmp(argv[i], "--interval")) {
            const char *arg = argv[i];
            long max = !strcmp(arg, "--top") ? SP_MAX_PROCESSES : !strcmp(arg, "--pid") ? 2147483647L : 5000;
            long value;
            if (i + 1 >= argc || positive_number(argv[++i], max, &value) ||
                (!strcmp(arg, "--interval") && value < 100)) {
                fprintf(stderr, "Invalid or missing value for %s\n", arg);
                return 2;
            }
            if (!strcmp(arg, "--top")) { opts.top = (unsigned int)value; opts.processes = 1; }
            else if (!strcmp(arg, "--pid")) { opts.pid = value; opts.top = 1; opts.processes = 1; }
            else opts.interval_ms = (unsigned int)value;
            continue;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        usage(stderr);
        return 2;
    }
    if (tui) {
        if (opts.json || opts.processes || opts.pid) {
            fputs("--tui cannot be combined with --json, --processes, --top or --pid\n", stderr);
            return 2;
        }
        return sp_run_tui();
    }
    SpSnapshot *snapshot = calloc(1, sizeof *snapshot);
    if (!snapshot) { perror("calloc"); return 1; }
    char error[256] = {0};
    if (sp_collect(snapshot, &opts, error, sizeof error)) {
        fprintf(stderr, "SysProbe: %s\n", error);
        free(snapshot);
        return 1;
    }
    if (opts.json) sp_print_json(snapshot, &opts);
    else sp_print_text(snapshot, &opts);
    free(snapshot);
    return 0;
}
