#include <stdint.h>
#include "fmt.h"

typedef struct {
    char  *buf;
    size_t size;
    size_t pos;
} sink_t;

static void put(sink_t *s, char c)
{
    if (s->pos + 1U < s->size) {
        s->buf[s->pos] = c;
    }
    s->pos++;
}

static void put_number(sink_t *s, uint32_t v, uint32_t base, const char *digits, int neg,
                       unsigned width, char pad)
{
    char tmp[11];
    unsigned n = 0U;
    unsigned len;

    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0U);
    len = n + (neg ? 1U : 0U);
    if (neg && pad == '0') {
        put(s, '-');
    }
    while (len < width) {
        put(s, pad);
        len++;
    }
    if (neg && pad != '0') {
        put(s, '-');
    }
    while (n != 0U) {
        put(s, tmp[--n]);
    }
}

int fmt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    sink_t s = { buf, size, 0U };

    while (*fmt != '\0') {
        char c = *fmt++;
        char pad = ' ';
        unsigned width = 0U;
        int is_long = 0;
        const char *start;

        if (c != '%') {
            put(&s, c);
            continue;
        }
        start = fmt - 1;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10U + (unsigned)(*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        switch (*fmt) {
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int neg = v < 0;

            put_number(&s, neg ? (uint32_t)(-(v + 1)) + 1U : (uint32_t)v, 10U, "0123456789",
                       neg, width, pad);
            break;
        }
        case 'u': {
            uint32_t v = is_long ? (uint32_t)va_arg(ap, unsigned long) : va_arg(ap, unsigned);

            put_number(&s, v, 10U, "0123456789", 0, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint32_t v = is_long ? (uint32_t)va_arg(ap, unsigned long) : va_arg(ap, unsigned);

            put_number(&s, v, 16U, (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF", 0,
                       width, pad);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);

            if (str == NULL) {
                str = "(null)";
            }
            while (*str != '\0') {
                put(&s, *str++);
            }
            break;
        }
        case 'c':
            put(&s, (char)va_arg(ap, int));
            break;
        case '%':
            put(&s, '%');
            break;
        default:
            while (start < fmt) {
                put(&s, *start++);
            }
            if (*fmt != '\0') {
                put(&s, *fmt);
            } else {
                fmt--;
            }
            break;
        }
        fmt++;
    }
    if (s.size != 0U) {
        s.buf[(s.pos < s.size) ? s.pos : s.size - 1U] = '\0';
    }
    return (int)s.pos;
}

int fmt_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = fmt_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}