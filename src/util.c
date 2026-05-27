#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

void kv_log(const char *level, const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t sec = (time_t)ts.tv_sec;

    struct tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &sec);
#else
    gmtime_r(&sec, &tm);
#endif

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &tm);
    fprintf(stderr, "[%s.%03ld] %s ", timebuf, (long)(ts.tv_nsec / 1000000), level);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
