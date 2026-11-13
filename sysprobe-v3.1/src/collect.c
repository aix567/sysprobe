#define _POSIX_C_SOURCE 200809L
#include "sysprobe.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static int read_file(const char *path, char *buffer, size_t capacity) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buffer, 1, capacity - 1, f);
    int failed = ferror(f) || (!feof(f) && n == capacity - 1);
    fclose(f);
    buffer[n] = '\0';
    return failed ? -1 : 0;
}

static int value_for(const char *text, const char *key, unsigned long long *out) {
    const size_t len = strlen(key);
    const char *p = text;
    while (*p) {
        if (strncmp(p, key, len) == 0 && p[len] == ':') {
            const char *num = p + len + 1;
            while (*num == ' ' || *num == '\t') ++num;
            if (!isdigit((unsigned char)*num)) return -1;
            errno = 0;
            char *end = NULL;
            unsigned long long v = strtoull(num, &end, 10);
            if (errno || end == num || (*end != '\n' && *end != ' ' && *end != '\t' && *end != '\0')) return -1;
            *out = v;
            return 0;
        }
        const char *next = strchr(p, '\n');
        if (!next) break;
        p = next + 1;
    }
    return -1;
}

int sp_parse_meminfo_text(const char *text, unsigned long long *total,
                          unsigned long long *available,
                          unsigned long long *swap_total, unsigned long long *swap_free) {
    if (!text || !total || !available || !swap_total || !swap_free) return -1;
    if (value_for(text, "MemTotal", total) || value_for(text, "MemAvailable", available) ||
        value_for(text, "SwapTotal", swap_total) || value_for(text, "SwapFree", swap_free)) return -1;
    return *total > 0 && *available <= *total && *swap_free <= *swap_total ? 0 : -1;
}

int sp_parse_cpu_text(const char *text, unsigned long long *idle, unsigned long long *total) {
    if (!text || !idle || !total || strncmp(text, "cpu ", 4) != 0) return -1;
    unsigned long long t[10] = {0};
    const char *p = text + 4;
    for (size_t i = 0; i < 8; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!isdigit((unsigned char)*p)) return -1;
        errno = 0;
        char *end = NULL;
        t[i] = strtoull(p, &end, 10);
        if (errno || p == end) return -1;
        p = end;
    }
    /* Linux /proc/stat: user nice system idle iowait irq softirq steal */
    *idle = t[3] + t[4];
    *total = 0;
    for (size_t i = 0; i < 8; ++i) *total += t[i];
    return 0;
}

int sp_cpu_usage(unsigned long long idle_a, unsigned long long total_a,
                 unsigned long long idle_b, unsigned long long total_b, double *percent) {
    if (!percent || total_b <= total_a || idle_b < idle_a) return -1;
    unsigned long long dt = total_b - total_a, di = idle_b - idle_a;
    if (di > dt) return -1;
    *percent = 100.0 * (double)(dt - di) / (double)dt;
    return 0;
}

static int read_cpu(unsigned long long *idle, unsigned long long *total) {
    char buffer[4096];
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    int ok = fgets(buffer, sizeof buffer, f) != NULL;
    fclose(f);
    return !ok || sp_parse_cpu_text(buffer, idle, total) ? -1 : 0;
}

static void sleep_ms(unsigned int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static void read_network(SpSnapshot *s) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        unsigned long long rx = 0, tx = 0, ignored[7];
        if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &rx, &ignored[0], &ignored[1], &ignored[2], &ignored[3],
                   &ignored[4], &ignored[5], &ignored[6], &tx) != 9) continue;
        /* Values are cumulative bytes since boot across all network interfaces, including loopback. */
        s->net_rx_bytes += rx;
        s->net_tx_bytes += tx;
    }
    fclose(f);
}

static int is_pid(const char *name) {
    if (!*name) return 0;
    while (*name) { if (!isdigit((unsigned char)*name)) return 0; ++name; }
    return 1;
}

static long count_fds(long pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/fd", pid);
    DIR *dir = opendir(path);
    if (!dir) return -1;
    long count = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++count;
    }
    closedir(dir);
    return count;
}

static int process_info(long pid, SpProcess *p, int want_fds) {
    char path[64], line[4096];
    snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    if (read_file(path, line, sizeof line)) return -1;
    char *left = strchr(line, '('), *right = strrchr(line, ')');
    if (!left || !right || right <= left || right[1] != ' ') return -1;
    size_t length = (size_t)(right - left - 1);
    if (length >= sizeof p->name) length = sizeof p->name - 1;
    memcpy(p->name, left + 1, length);
    p->name[length] = '\0';
    p->pid = pid;
    char *rest = right + 2;
    p->state = *rest;
    if (p->state == '\0' || rest[1] != ' ') return -1;
    /* tokens after state: ppid=4 ... utime=14 stime=15 */
    rest += 2;
    unsigned long long utime = 0, stime = 0;
    for (int field = 4; field <= 22; ++field) {
        while (*rest == ' ') ++rest;
        if (!*rest) return -1;
        char *end = rest;
        while (*end && *end != ' ' && *end != '\n') ++end;
        if (field == 14 || field == 15 || field == 22) {
            char saved = *end;
            *end = '\0';
            errno = 0;
            char *parsed;
            unsigned long long n = strtoull(rest, &parsed, 10);
            if (errno || parsed == rest || *parsed) return -1;
            if (field == 14) utime = n; else if (field == 15) stime = n; else p->start_ticks = n;
            *end = saved;
        }
        rest = end;
    }
    p->cpu_ticks = utime + stime;
    p->rss_kib = 0;
    snprintf(path, sizeof path, "/proc/%ld/status", pid);
    char status[16384];
    if (!read_file(path, status, sizeof status)) {
        unsigned long long rss;
        if (!value_for(status, "VmRSS", &rss)) p->rss_kib = rss;
        unsigned long long uid;
        if (!value_for(status, "Uid", &uid)) p->uid = (unsigned long)uid;
    }
    p->fd_count = want_fds ? count_fds(pid) : -1;
    return 0;
}

static int compare_proc(const void *a, const void *b) {
    const SpProcess *pa = a, *pb = b;
    if (pa->rss_kib < pb->rss_kib) return 1;
    if (pa->rss_kib > pb->rss_kib) return -1;
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

/* Select the N largest resident-memory processes, bounded to SP_MAX_PROCESSES. */
static void collect_processes(SpSnapshot *s, const SpOptions *opts) {
    DIR *dir = opendir("/proc");
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!is_pid(e->d_name)) continue;
        errno = 0;
        char *end = NULL;
        long pid = strtol(e->d_name, &end, 10);
        if (errno || pid <= 0 || *end) continue;
        if (opts->pid > 0 && pid != opts->pid) continue;
        SpProcess proc = {0};
        if (process_info(pid, &proc, opts->pid > 0)) continue;
        if (s->process_count < opts->top) {
            s->processes[s->process_count++] = proc;
        } else {
            size_t min = 0;
            for (size_t i = 1; i < s->process_count; ++i)
                if (s->processes[i].rss_kib < s->processes[min].rss_kib) min = i;
            if (s->process_count && proc.rss_kib > s->processes[min].rss_kib)
                s->processes[min] = proc;
        }
    }
    closedir(dir);
    qsort(s->processes, s->process_count, sizeof s->processes[0], compare_proc);
}

int sp_collect(SpSnapshot *s, const SpOptions *opts, char *error, size_t cap) {
    if (!s || !opts || !error || cap == 0) return -1;
    memset(s, 0, sizeof *s);
    struct utsname info;
    if (uname(&info)) { snprintf(error, cap, "uname: %s", strerror(errno)); return -1; }
    snprintf(s->sysname, sizeof s->sysname, "%s", info.sysname);
    snprintf(s->nodename, sizeof s->nodename, "%s", info.nodename);
    snprintf(s->release, sizeof s->release, "%s", info.release);
    snprintf(s->machine, sizeof s->machine, "%s", info.machine);
    char mem[8192];
    if (read_file("/proc/meminfo", mem, sizeof mem) ||
        sp_parse_meminfo_text(mem, &s->mem_total_kib, &s->mem_available_kib,
                              &s->swap_total_kib, &s->swap_free_kib)) {
        snprintf(error, cap, "cannot parse /proc/meminfo"); return -1;
    }
    unsigned long long a_idle, a_total, b_idle, b_total;
    if (read_cpu(&a_idle, &a_total)) { snprintf(error, cap, "cannot read /proc/stat"); return -1; }
    sleep_ms(opts->interval_ms);
    if (read_cpu(&b_idle, &b_total) || sp_cpu_usage(a_idle, a_total, b_idle, b_total, &s->cpu_percent)) {
        snprintf(error, cap, "cannot calculate CPU usage"); return -1;
    }
    struct statvfs disk;
    if (statvfs("/", &disk) == 0) {
        s->disk_total_bytes = (unsigned long long)disk.f_blocks * disk.f_frsize;
        s->disk_available_bytes = (unsigned long long)disk.f_bavail * disk.f_frsize;
    }
    read_network(s);
    if (opts->processes) collect_processes(s, opts);
    return 0;
}
