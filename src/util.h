#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

int64_t now_ms(void);
void kv_log(const char *level, const char *fmt, ...);

#endif /* UTIL_H */
