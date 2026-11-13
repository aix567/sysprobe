#ifndef SYSPROBE_H
#define SYSPROBE_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define SP_NAME_MAX 256
#define SP_MAX_PROCESSES 2048

typedef struct {
    char name[SP_NAME_MAX];
    long pid;
    char state;
    unsigned long long cpu_ticks;
    unsigned long long rss_kib;
    long fd_count; /* -1 means inaccessible */
    unsigned long long start_ticks;
    unsigned long uid;
    double cpu_live;
} SpProcess;

typedef struct {
    char sysname[128], nodename[128], release[128], machine[128];
    unsigned long long mem_total_kib, mem_available_kib, swap_total_kib, swap_free_kib;
    unsigned long long net_rx_bytes, net_tx_bytes;
    unsigned long long disk_total_bytes, disk_available_bytes;
    double cpu_percent;
    SpProcess processes[SP_MAX_PROCESSES];
    size_t process_count;
} SpSnapshot;

typedef struct {
    unsigned int interval_ms;
    unsigned int top;
    long pid;
    int json;
    int processes;
} SpOptions;

int sp_collect(SpSnapshot *out, const SpOptions *opts, char *error, size_t error_cap);
int sp_parse_meminfo_text(const char *text, unsigned long long *total,
                          unsigned long long *available,
                          unsigned long long *swap_total, unsigned long long *swap_free);
int sp_parse_cpu_text(const char *text, unsigned long long *idle,
                      unsigned long long *total);
int sp_cpu_usage(unsigned long long idle_a, unsigned long long total_a,
                 unsigned long long idle_b, unsigned long long total_b, double *percent);
int sp_run_tui(void);
void sp_print_text(const SpSnapshot *s, const SpOptions *opts);
void sp_print_json(const SpSnapshot *s, const SpOptions *opts);
#endif
