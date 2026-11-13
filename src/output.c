#include "sysprobe.h"
#include <stdio.h>

static void json_string(const char *s) {
    putchar('"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default: if (c < 32) printf("\\u%04x", c); else putchar(c);
        }
    }
    putchar('"');
}

void sp_print_text(const SpSnapshot *s, const SpOptions *opts) {
    printf("SysProbe v2.0 | Linux system diagnostics\n");
    printf("Host: %s | OS: %s | Kernel: %s | Arch: %s\n",
           s->nodename, s->sysname, s->release, s->machine);
    printf("CPU usage (sampled): %.1f%%\n", s->cpu_percent);
    printf("RAM: %.2f / %.2f GiB used (estimated via MemAvailable)\n",
           (double)(s->mem_total_kib - s->mem_available_kib) / 1048576.0,
           (double)s->mem_total_kib / 1048576.0);
    printf("Swap: %.2f / %.2f GiB used\n",
           (double)(s->swap_total_kib - s->swap_free_kib) / 1048576.0,
           (double)s->swap_total_kib / 1048576.0);
    if (s->disk_total_bytes)
        printf("Root filesystem: %.2f / %.2f GiB used (includes reserved blocks)\n",
               (double)(s->disk_total_bytes - s->disk_available_bytes) / 1073741824.0,
               (double)s->disk_total_bytes / 1073741824.0);
    printf("Network totals since boot, all interfaces: RX %.2f MiB | TX %.2f MiB\n",
           (double)s->net_rx_bytes / 1048576.0, (double)s->net_tx_bytes / 1048576.0);
    if (opts->processes) {
        printf("\nProcesses (sorted by RSS; CPU ticks are cumulative):\n");
        printf("%7s %-24s %5s %12s %12s %8s\n", "PID", "NAME", "STATE", "RSS MiB", "CPU TICKS", "FDs");
        for (size_t i = 0; i < s->process_count; ++i) {
            const SpProcess *p = &s->processes[i];
            if (p->fd_count < 0)
                printf("%7ld %-24.24s %5c %12.1f %12llu %8s\n", p->pid, p->name, p->state,
                       (double)p->rss_kib / 1024.0, p->cpu_ticks, "N/A");
            else
                printf("%7ld %-24.24s %5c %12.1f %12llu %8ld\n", p->pid, p->name, p->state,
                       (double)p->rss_kib / 1024.0, p->cpu_ticks, p->fd_count);
        }
        if (opts->pid > 0 && !s->process_count)
            fprintf(stderr, "Notice: PID %ld not found or inaccessible.\n", opts->pid);
    }
}

void sp_print_json(const SpSnapshot *s, const SpOptions *opts) {
    fputs("{\"version\":\"1.0.0\",\"system\":{\"hostname\":", stdout);
    json_string(s->nodename);
    fputs(",\"os\":", stdout); json_string(s->sysname);
    fputs(",\"kernel\":", stdout); json_string(s->release);
    fputs(",\"arch\":", stdout); json_string(s->machine);
    printf("},\"cpu_percent\":%.2f,\"memory\":{\"total_kib\":%llu,\"available_kib\":%llu,"
           "\"swap_total_kib\":%llu,\"swap_free_kib\":%llu},"
           "\"root_disk\":{\"total_bytes\":%llu,\"available_bytes\":%llu},"
           "\"network_since_boot\":{\"rx_bytes\":%llu,\"tx_bytes\":%llu},\"processes\":[",
           s->cpu_percent, s->mem_total_kib, s->mem_available_kib,
           s->swap_total_kib, s->swap_free_kib, s->disk_total_bytes,
           s->disk_available_bytes, s->net_rx_bytes, s->net_tx_bytes);
    if (opts->processes) for (size_t i = 0; i < s->process_count; ++i) {
        const SpProcess *p = &s->processes[i];
        if (i) putchar(',');
        printf("{\"pid\":%ld,\"name\":", p->pid); json_string(p->name);
        printf(",\"state\":\"%c\",\"rss_kib\":%llu,\"cpu_ticks\":%llu,\"fd_count\":",
               p->state, p->rss_kib, p->cpu_ticks);
        if (p->fd_count < 0) fputs("null", stdout); else printf("%ld", p->fd_count);
        putchar('}');
    }
    fputs("]}\n", stdout);
}
