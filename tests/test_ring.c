#include <stdint.h>
#include "minitest.h"
#include "plat_ring.h"

static plat_ring_t ring;

static void test_a_new_ring_is_empty(void)
{
    uint8_t b = 0x55U;

    plat_ring_init(&ring);
    CHECK_EQ(plat_ring_count(&ring), 0);
    CHECK_EQ(plat_ring_get(&ring, &b), -1);
    CHECK_EQ(b, 0x55);
    CHECK_EQ(ring.dropped, 0);
    CHECK_EQ(ring.high, 0);
}

static void test_bytes_come_out_in_the_order_they_went_in(void)
{
    uint8_t b = 0U;

    plat_ring_init(&ring);
    CHECK_EQ(plat_ring_put(&ring, 'a'), 0);
    CHECK_EQ(plat_ring_put(&ring, 'b'), 0);
    CHECK_EQ(plat_ring_put(&ring, 'c'), 0);
    CHECK_EQ(plat_ring_count(&ring), 3);
    CHECK_EQ(plat_ring_get(&ring, &b), 0);
    CHECK_EQ(b, 'a');
    CHECK_EQ(plat_ring_get(&ring, &b), 0);
    CHECK_EQ(b, 'b');
    CHECK_EQ(plat_ring_put(&ring, 'd'), 0);
    CHECK_EQ(plat_ring_get(&ring, &b), 0);
    CHECK_EQ(b, 'c');
    CHECK_EQ(plat_ring_get(&ring, &b), 0);
    CHECK_EQ(b, 'd');
    CHECK_EQ(plat_ring_get(&ring, &b), -1);
}

static void test_a_full_ring_refuses_the_byte_and_counts_it(void)
{
    unsigned i;
    uint8_t b = 0U;

    plat_ring_init(&ring);
    for (i = 0U; i < PLAT_RING_SIZE; i++) {
        CHECK_EQ(plat_ring_put(&ring, (uint8_t)i), 0);
    }
    CHECK_EQ(plat_ring_count(&ring), PLAT_RING_SIZE);
    CHECK_EQ(plat_ring_put(&ring, 0xEEU), -1);
    CHECK_EQ(plat_ring_put(&ring, 0xEEU), -1);
    CHECK_EQ(ring.dropped, 2);
    CHECK_EQ(plat_ring_count(&ring), PLAT_RING_SIZE);
    for (i = 0U; i < PLAT_RING_SIZE; i++) {
        CHECK_EQ(plat_ring_get(&ring, &b), 0);
        CHECK_EQ(b, (uint8_t)i);
    }
    CHECK_EQ(plat_ring_get(&ring, &b), -1);
}

static void test_a_slot_freed_by_get_can_be_used_again(void)
{
    unsigned i;
    uint8_t b = 0U;

    plat_ring_init(&ring);
    for (i = 0U; i < PLAT_RING_SIZE; i++) {
        (void)plat_ring_put(&ring, 1U);
    }
    CHECK_EQ(plat_ring_put(&ring, 2U), -1);
    CHECK_EQ(plat_ring_get(&ring, &b), 0);
    CHECK_EQ(plat_ring_put(&ring, 2U), 0);
    CHECK_EQ(plat_ring_count(&ring), PLAT_RING_SIZE);
}

static void test_the_indices_survive_the_16_bit_wrap(void)
{
    uint32_t i;
    uint8_t b = 0U;
    unsigned bad = 0U;

    plat_ring_init(&ring);
    for (i = 0U; i < 200000U; i++) {
        if (plat_ring_put(&ring, (uint8_t)(i * 7U)) != 0) {
            bad++;
        }
        if (plat_ring_get(&ring, &b) != 0 || b != (uint8_t)(i * 7U)) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(plat_ring_count(&ring), 0);
    CHECK_EQ(ring.head, (uint16_t)(200000U & 0xFFFFU));
}

static void test_the_count_is_right_when_the_head_has_wrapped_past_the_tail(void)
{
    unsigned i;
    uint8_t b = 0U;

    plat_ring_init(&ring);
    ring.head = 65530U;
    ring.tail = 65530U;
    for (i = 0U; i < 20U; i++) {
        CHECK_EQ(plat_ring_put(&ring, (uint8_t)i), 0);
    }
    CHECK_EQ(ring.head, 14);
    CHECK_EQ(plat_ring_count(&ring), 20);
    for (i = 0U; i < 20U; i++) {
        CHECK_EQ(plat_ring_get(&ring, &b), 0);
        CHECK_EQ(b, (uint8_t)i);
    }
}

static void test_the_high_water_mark_keeps_the_fullest_moment(void)
{
    unsigned i;
    uint8_t b = 0U;

    plat_ring_init(&ring);
    for (i = 0U; i < 10U; i++) {
        (void)plat_ring_put(&ring, 0U);
    }
    for (i = 0U; i < 7U; i++) {
        (void)plat_ring_get(&ring, &b);
    }
    (void)plat_ring_put(&ring, 0U);
    CHECK_EQ(plat_ring_count(&ring), 4);
    CHECK_EQ(ring.high, 10);
}

static void test_reset_stats_clears_the_drops_and_restarts_the_high_water_mark(void)
{
    unsigned i;

    plat_ring_init(&ring);
    for (i = 0U; i < PLAT_RING_SIZE + 3U; i++) {
        (void)plat_ring_put(&ring, 0U);
    }
    CHECK_EQ(ring.dropped, 3);
    CHECK_EQ(ring.high, PLAT_RING_SIZE);
    plat_ring_reset_stats(&ring);
    CHECK_EQ(ring.dropped, 0);
    CHECK_EQ(ring.high, PLAT_RING_SIZE);
    for (i = 0U; i < PLAT_RING_SIZE; i++) {
        uint8_t b;

        (void)plat_ring_get(&ring, &b);
    }
    plat_ring_reset_stats(&ring);
    CHECK_EQ(ring.high, 0);
    (void)plat_ring_put(&ring, 0U);
    CHECK_EQ(ring.high, 1);
}

int main(void)
{
    RUN(test_a_new_ring_is_empty);
    RUN(test_bytes_come_out_in_the_order_they_went_in);
    RUN(test_a_full_ring_refuses_the_byte_and_counts_it);
    RUN(test_a_slot_freed_by_get_can_be_used_again);
    RUN(test_the_indices_survive_the_16_bit_wrap);
    RUN(test_the_count_is_right_when_the_head_has_wrapped_past_the_tail);
    RUN(test_the_high_water_mark_keeps_the_fullest_moment);
    RUN(test_reset_stats_clears_the_drops_and_restarts_the_high_water_mark);
    return REPORT();
}
