#include "sysprobe.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    unsigned long long total, avail, swap, swapfree;
    assert(sp_parse_meminfo_text("MemTotal: 8192 kB\nMemAvailable: 4096 kB\nSwapTotal: 2048 kB\nSwapFree: 1024 kB\n", &total, &avail, &swap, &swapfree) == 0);
    assert(total == 8192 && avail == 4096 && swap == 2048 && swapfree == 1024);
    assert(sp_parse_meminfo_text("MemTotal: 1 kB\nMemAvailable: 2 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n", &total, &avail, &swap, &swapfree) != 0);
    assert(sp_parse_meminfo_text("invalid", &total, &avail, &swap, &swapfree) != 0);
    unsigned long long idle, ticks;
    assert(sp_parse_cpu_text("cpu  10 0 30 50 5 2 1 2 0 0\n", &idle, &ticks) == 0);
    assert(idle == 55 && ticks == 100);
    assert(sp_parse_cpu_text("not cpu", &idle, &ticks) != 0);
    double pct;
    assert(sp_cpu_usage(100, 200, 130, 300, &pct) == 0);
    assert(fabs(pct - 70.0) < 0.001);
    assert(sp_cpu_usage(100, 200, 50, 300, &pct) != 0);
    assert(sp_cpu_usage(100, 200, 110, 200, &pct) != 0);
    puts("All unit tests passed");
    return 0;
}
