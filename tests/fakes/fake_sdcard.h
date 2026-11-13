#ifndef FAKE_SDCARD_H
#define FAKE_SDCARD_H

#include <stddef.h>
#include <stdint.h>
#include "plat_spi.h"


#define FAKE_SD_FRAME_HISTORY 64U

void fake_sd_reset(void);

void fake_sd_set_silent(uint8_t silent);
void fake_sd_set_version1(uint8_t v1);
void fake_sd_set_high_capacity(uint8_t hc);
void fake_sd_set_acmd41_busy_polls(int polls);
void fake_sd_set_cmd8_echo(uint32_t value);
void fake_sd_set_read_r1(uint8_t r1);
void fake_sd_set_read_token(uint8_t token);
void fake_sd_set_write_r1(uint8_t r1);
void fake_sd_set_write_response(uint8_t token);
void fake_sd_set_write_busy_polls(unsigned polls);
void fake_sd_set_block(uint32_t index, const uint8_t *data512);
void fake_sd_fail_transfer(unsigned call_index, plat_status_t status);
void fake_sd_fail_spi_init(plat_status_t status);

size_t           fake_sd_cmd_count(void);
const uint8_t   *fake_sd_frame_at(size_t index);
uint8_t          fake_sd_cmd_at(size_t index);
uint32_t         fake_sd_arg_at(size_t index);
plat_spi_speed_t fake_sd_speed_at(size_t index);
unsigned         fake_sd_idle_bytes_before_first_cmd(void);
unsigned         fake_sd_transfer_calls(void);
unsigned         fake_sd_write_count(void);
uint32_t         fake_sd_stored_index(void);
const uint8_t   *fake_sd_stored_block(void);
unsigned         fake_sd_write_count(void);
uint32_t         fake_sd_stored_index(void);
const uint8_t   *fake_sd_stored_block(void);
uint8_t          fake_sd_cs_asserted(void);
plat_spi_speed_t fake_sd_speed(void);

#endif /* FAKE_SDCARD_H */
