#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include "plat_status.h"
#include "sdcard.h"

#define LOGGER_RECORD_SIZE        32U
#define LOGGER_RECORDS_PER_BLOCK  (SDCARD_BLOCK_SIZE / LOGGER_RECORD_SIZE)
#define LOGGER_FIRST_BLOCK        8192U
#define LOGGER_BLOCKS             4096U
#define LOGGER_MAX_AGE_MS         10000U
#define LOGGER_RETRY_MS           1000U
#define LOGGER_SENSORS            4U

#define LOGGER_F_AZ    0x01U
#define LOGGER_F_TILT  0x02U
#define LOGGER_F_PARK  0x04U
#define LOGGER_F_STOW  0x08U

typedef struct {
    uint32_t seq;
    uint32_t t_ms;
    uint16_t raw[LOGGER_SENSORS];
    uint8_t  az;
    uint8_t  tilt;
    uint8_t  flags;
} logger_record_t;

typedef struct {
    uint8_t  buf[SDCARD_BLOCK_SIZE];
    uint8_t  count;
    uint8_t  saved;
    uint8_t  retry_wait;
    uint32_t blocks;
    uint32_t first_ms;
    uint32_t retry_ms;
    uint32_t dropped;
    uint32_t errors;
} logger_t;

void logger_init(logger_t *lg);

void logger_encode(uint8_t out[LOGGER_RECORD_SIZE], const logger_record_t *r);

int logger_decode(const uint8_t in[LOGGER_RECORD_SIZE], logger_record_t *r);

int logger_add(logger_t *lg, uint32_t now_ms, const uint16_t raw[LOGGER_SENSORS], uint8_t az,
               uint8_t tilt, uint8_t flags);

int logger_due(const logger_t *lg, uint32_t now_ms);

plat_status_t logger_flush(logger_t *lg, const sdcard_t *sd, uint32_t now_ms);

void logger_failed(logger_t *lg, uint32_t now_ms);

uint32_t logger_total(const logger_t *lg);

uint32_t logger_stored(const logger_t *lg);

plat_status_t logger_read(const sdcard_t *sd, uint32_t n, uint8_t *block, logger_record_t *r,
                          int *valid);

#endif /* LOGGER_H */