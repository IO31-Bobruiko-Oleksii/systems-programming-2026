#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stddef.h>

/* replacement for vsnprint */
int fmt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int fmt_snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* FMT_H */