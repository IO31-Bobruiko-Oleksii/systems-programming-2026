#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "minitest.h"
#include "fmt.h"

#define SAME(...) \
    do { \
        char want_[64]; \
        char got_[64]; \
        int nw_ = snprintf(want_, sizeof want_, __VA_ARGS__); \
        int ng_ = fmt_snprintf(got_, sizeof got_, __VA_ARGS__); \
        CHECK_EQ(ng_, nw_); \
        CHECK(strcmp(got_, want_) == 0); \
    } while (0)

static void test_unsigned_conversions_match_libc(void)
{
    SAME("%u", 0U);
    SAME("%u", 7U);
    SAME("%u", 4294967295U);
    SAME("%lu", 0UL);
    SAME("%lu", 123456789UL);
    SAME("%lu", (unsigned long)4294967295U);
    SAME("n=%lu min_us=%lu", (unsigned long)5000U, (unsigned long)602U);
}

static void test_signed_conversions_match_libc(void)
{
    SAME("%d", 0);
    SAME("%d", 42);
    SAME("%d", -42);
    SAME("%d", INT_MAX);
    SAME("%d", INT_MIN);
    SAME("status=%d", -3);
}

static void test_hex_and_width_match_libc(void)
{
    SAME("%02X", 0U);
    SAME("%02X", 5U);
    SAME("%02X", 255U);
    SAME("%02X", 4095U);
    SAME("%x", 48879U);
    SAME("%X", 48879U);
    SAME("%08lX", (unsigned long)0xA5A5U);
    SAME("%5u|", 42U);
    SAME("%05u|", 42U);
    SAME("%5d|", -42);
    SAME("%05d|", -42);
    SAME("%2u", 12345U);
}

static void test_text_conversions_match_libc(void)
{
    SAME("%s", "abc");
    SAME("%s", "");
    SAME("CMD %s FAIL reason=unknown", "frob");
    SAME("%c%c", 'o', 'k');
    SAME("100%%");
    SAME("no conversions at all");
}

static void test_a_null_string_prints_null_in_brackets(void)
{
    char buf[16];

    CHECK_EQ(fmt_snprintf(buf, sizeof buf, "%s", (const char *)NULL), 6);
    CHECK(strcmp(buf, "(null)") == 0);
}

static void test_a_small_buffer_is_cut_and_the_needed_length_is_returned(void)
{
    char buf[8];

    CHECK_EQ(fmt_snprintf(buf, sizeof buf, "STAT name=%s", "loop"), 14);
    CHECK(strcmp(buf, "STAT na") == 0);
    CHECK_EQ(fmt_snprintf(buf, 1U, "abc"), 3);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(fmt_snprintf(NULL, 0U, "abc%u", 12U), 5);
}

static void test_an_unknown_conversion_is_copied(void)
{
    char buf[16];

    CHECK_EQ(fmt_snprintf(buf, sizeof buf, "a%qb"), 4);
    CHECK(strcmp(buf, "a%qb") == 0);
    CHECK_EQ(fmt_snprintf(buf, sizeof buf, "a%"), 2);
    CHECK(strcmp(buf, "a%") == 0);
}

int main(void)
{
    RUN(test_unsigned_conversions_match_libc);
    RUN(test_signed_conversions_match_libc);
    RUN(test_hex_and_width_match_libc);
    RUN(test_text_conversions_match_libc);
    RUN(test_a_null_string_prints_null_in_brackets);
    RUN(test_a_small_buffer_is_cut_and_the_needed_length_is_returned);
    RUN(test_an_unknown_conversion_is_copied);
    return REPORT();
}