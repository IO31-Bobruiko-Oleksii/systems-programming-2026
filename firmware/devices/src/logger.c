#include <stddef.h>
#include <string.h>
#include "logger.h"

#define CHECK_BYTES  28U
#define CHECK_SEED   0xA5A5A5A5UL

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t check_of(const uint8_t *rec)
{
    uint32_t sum = 0U;
    unsigned i;

    for (i = 0U; i < CHECK_BYTES; i++) {
        sum += rec[i];
    }
    return sum ^ (uint32_t)CHECK_SEED;
}

void logger_init(logger_t *lg)
{
    if (lg == NULL) {
        return;
    }
    memset(lg, 0, sizeof *lg);
}

void logger_encode(uint8_t out[LOGGER_RECORD_SIZE], const logger_record_t *r)
{
    unsigned i;

    if (out == NULL || r == NULL) {
        return;
    }
    memset(out, 0, LOGGER_RECORD_SIZE);
    put32(out, r->seq);
    put32(out + 4, r->t_ms);
    for (i = 0U; i < LOGGER_SENSORS; i++) {
        put16(out + 8U + 2U * i, r->raw[i]);
    }
    out[16] = r->az;
    out[17] = r->tilt;
    out[18] = r->flags;
    put32(out + CHECK_BYTES, check_of(out));
}

int logger_decode(const uint8_t in[LOGGER_RECORD_SIZE], logger_record_t *r)
{
    unsigned i;

    if (in == NULL || r == NULL) {
        return 0;
    }
    r->seq = get32(in);
    r->t_ms = get32(in + 4);
    for (i = 0U; i < LOGGER_SENSORS; i++) {
        r->raw[i] = get16(in + 8U + 2U * i);
    }
    r->az = in[16];
    r->tilt = in[17];
    r->flags = in[18];
    return get32(in + CHECK_BYTES) == check_of(in);
}

uint32_t logger_total(const logger_t *lg)
{
    return (lg == NULL) ? 0U : lg->blocks * LOGGER_RECORDS_PER_BLOCK + lg->count;
}

uint32_t logger_stored(const logger_t *lg)
{
    return (lg == NULL) ? 0U : lg->blocks * LOGGER_RECORDS_PER_BLOCK + lg->saved;
}

int logger_add(logger_t *lg, uint32_t now_ms, const uint16_t raw[LOGGER_SENSORS], uint8_t az,
               uint8_t tilt, uint8_t flags)
{
    logger_record_t r;
    unsigned i;

    if (lg == NULL || raw == NULL) {
        return -1;
    }
    if (lg->blocks >= LOGGER_BLOCKS || lg->count >= LOGGER_RECORDS_PER_BLOCK) {
        lg->dropped++;
        return -1;
    }
    r.seq = logger_total(lg);
    r.t_ms = now_ms;
    for (i = 0U; i < LOGGER_SENSORS; i++) {
        r.raw[i] = raw[i];
    }
    r.az = az;
    r.tilt = tilt;
    r.flags = flags;
    if (lg->count == lg->saved) {
        lg->first_ms = now_ms;
    }
    logger_encode(lg->buf + (size_t)lg->count * LOGGER_RECORD_SIZE, &r);
    lg->count++;
    return 0;
}

int logger_due(const logger_t *lg, uint32_t now_ms)
{
    if (lg == NULL || lg->count == lg->saved) {
        return 0;
    }
    if (lg->retry_wait != 0U && (int32_t)(now_ms - lg->retry_ms) < 0) {
        return 0;
    }
    return lg->count >= LOGGER_RECORDS_PER_BLOCK ||
           (uint32_t)(now_ms - lg->first_ms) >= LOGGER_MAX_AGE_MS;
}

void logger_failed(logger_t *lg, uint32_t now_ms)
{
    if (lg == NULL) {
        return;
    }
    lg->errors++;
    lg->retry_wait = 1U;
    lg->retry_ms = now_ms + LOGGER_RETRY_MS;
}

plat_status_t logger_flush(logger_t *lg, const sdcard_t *sd, uint32_t now_ms)
{
    plat_status_t st;

    if (lg == NULL || sd == NULL || lg->blocks >= LOGGER_BLOCKS) {
        return PLAT_ERR_ARG;
    }
    if (lg->count == lg->saved) {
        return PLAT_OK;
    }
    st = sdcard_write_block(sd, LOGGER_FIRST_BLOCK + lg->blocks, lg->buf);
    if (st != PLAT_OK) {
        logger_failed(lg, now_ms);
        return st;
    }
    lg->retry_wait = 0U;
    lg->saved = lg->count;
    if (lg->count >= LOGGER_RECORDS_PER_BLOCK) {
        lg->blocks++;
        lg->count = 0U;
        lg->saved = 0U;
        memset(lg->buf, 0, sizeof lg->buf);
    }
    return PLAT_OK;
}

plat_status_t logger_read(const sdcard_t *sd, uint32_t n, uint8_t *block, logger_record_t *r,
                          int *valid)
{
    plat_status_t st;

    if (sd == NULL || block == NULL || r == NULL || valid == NULL) {
        return PLAT_ERR_ARG;
    }
    st = sdcard_read_block(sd, LOGGER_FIRST_BLOCK + n / LOGGER_RECORDS_PER_BLOCK, block);
    if (st != PLAT_OK) {
        return st;
    }
    *valid = logger_decode(block + (size_t)(n % LOGGER_RECORDS_PER_BLOCK) * LOGGER_RECORD_SIZE, r);
    return PLAT_OK;
}