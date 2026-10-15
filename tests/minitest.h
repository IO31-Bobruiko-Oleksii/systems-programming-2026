#ifndef MINITEST_H
#define MINITEST_H

#include <stdio.h>

static int mt_checks = 0;
static int mt_failures = 0;

#define CHECK(cond) \
    do { \
        mt_checks++; \
        if (!(cond)) { \
            mt_failures++; \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        long long a_ = (long long)(actual); \
        long long e_ = (long long)(expected); \
        mt_checks++; \
        if (a_ != e_) { \
            mt_failures++; \
            printf("    FAIL %s:%d: %s == %s (got %lld, expected %lld)\n", \
                   __FILE__, __LINE__, #actual, #expected, a_, e_); \
        } \
    } while (0)

#define RUN(fn) \
    do { \
        printf("  %s\n", #fn); \
        fn(); \
    } while (0)

#define REPORT() \
    (printf("%s: %d checks, %d failed\n", __FILE__, mt_checks, mt_failures), \
     (mt_failures != 0) ? 1 : 0)

#endif /* MINITEST_H */
