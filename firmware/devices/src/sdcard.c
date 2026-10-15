#include <stddef.h>
#include "sdcard.h"
#include "plat_spi.h"

#define CMD0    0U
#define CMD8    8U
#define CMD17   17U
#define CMD41   41U
#define CMD55   55U
#define CMD58   58U

#define CRC_CMD0   0x95U
#define CRC_CMD8   0x87U
#define CRC_DUMMY  0x01U

#define R1_IDLE     0x01U
#define R1_ILLEGAL  0x04U

#define CMD8_ARG           0x000001AAU
#define ACMD41_ARG_HCS     0x40000000U
#define TOKEN_START_BLOCK  0xFEU
#define IDLE_BYTE          0xFFU
#define POWER_UP_BYTES     10U

static plat_status_t xfer_byte(uint8_t tx, uint8_t *rx)
{
    return plat_spi_transfer(&tx, rx, 1U);
}

static plat_status_t read_bytes(uint8_t *buf, size_t len)
{
    return plat_spi_transfer(NULL, buf, len);
}

static plat_status_t command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r1)
{
    uint8_t frame[6];
    uint8_t b = IDLE_BYTE;
    plat_status_t st;
    unsigned i;

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = crc;

    st = plat_spi_transfer(frame, NULL, sizeof frame);
    for (i = 0U; st == PLAT_OK && i < SDCARD_R1_POLLS; i++) {
        st = xfer_byte(IDLE_BYTE, &b);
        if (st == PLAT_OK && (b & 0x80U) == 0U) {
            *r1 = b;
            return PLAT_OK;
        }
    }
    return (st == PLAT_OK) ? PLAT_ERR_TIMEOUT : st;
}

static plat_status_t release(void)
{
    plat_spi_cs(0U);
    return xfer_byte(IDLE_BYTE, NULL);
}

static plat_status_t go_idle(void)
{
    uint8_t r1 = 0U;
    plat_status_t st = PLAT_ERR_TIMEOUT;
    unsigned i;

    for (i = 0U; i < SDCARD_CMD0_TRIES; i++) {
        st = command(CMD0, 0U, CRC_CMD0, &r1);
        if (st == PLAT_OK) {
            if (r1 == R1_IDLE) {
                return PLAT_OK;
            }
            st = PLAT_ERR_BUS;
        } else if (st != PLAT_ERR_TIMEOUT) {
            return st;
        }
    }
    return st;
}

static plat_status_t check_interface(int *v2)
{
    uint8_t r1 = 0U;
    uint8_t r7[4];
    plat_status_t st = command(CMD8, CMD8_ARG, CRC_CMD8, &r1);

    if (st != PLAT_OK) {
        return st;
    }
    if (r1 == (R1_IDLE | R1_ILLEGAL)) { 
        *v2 = 0;
        return PLAT_OK;
    }
    if (r1 != R1_IDLE) {
        return PLAT_ERR_BUS;
    }
    st = read_bytes(r7, sizeof r7);
    if (st != PLAT_OK) {
        return st;
    }
    if ((r7[2] & 0x0FU) != 0x01U || r7[3] != 0xAAU) { 
        return PLAT_ERR_BUS;
    }
    *v2 = 1;
    return PLAT_OK;
}

static plat_status_t wait_ready(int v2)
{
    uint8_t r1 = 0U;
    plat_status_t st;
    unsigned i;

    for (i = 0U; i < SDCARD_ACMD41_TRIES; i++) {
        st = command(CMD55, 0U, CRC_DUMMY, &r1);
        if (st != PLAT_OK) {
            return st;
        }
        if (r1 > R1_IDLE) {
            return PLAT_ERR_BUS;
        }
        st = command(CMD41, v2 ? ACMD41_ARG_HCS : 0U, CRC_DUMMY, &r1);
        if (st != PLAT_OK) {
            return st;
        }
        if (r1 == 0U) {
            return PLAT_OK;
        }
        if (r1 != R1_IDLE) {
            return PLAT_ERR_BUS;
        }
    }
    return PLAT_ERR_TIMEOUT;
}

static plat_status_t read_capacity_class(sd_type_t *type)
{
    uint8_t r1 = 0U;
    uint8_t ocr[4];
    plat_status_t st = command(CMD58, 0U, CRC_DUMMY, &r1);

    if (st != PLAT_OK) {
        return st;
    }
    if (r1 != 0U) {
        return PLAT_ERR_BUS;
    }
    st = read_bytes(ocr, sizeof ocr);
    if (st != PLAT_OK) {
        return st;
    }
    if ((ocr[0] & 0x80U) == 0U) {
        return PLAT_ERR_BUS;
    }
    *type = ((ocr[0] & 0x40U) != 0U) ? SD_TYPE_V2_HC : SD_TYPE_V2;
    return PLAT_OK;
}

static plat_status_t negotiate(sd_type_t *type)
{
    int v2 = 0;
    plat_status_t st = go_idle();

    if (st == PLAT_OK) {
        st = check_interface(&v2);
    }
    if (st == PLAT_OK) {
        st = wait_ready(v2);
    }
    if (st == PLAT_OK) {
        *type = SD_TYPE_V1;
        if (v2) {
            st = read_capacity_class(type);
        }
    }
    return st;
}

plat_status_t sdcard_init(sdcard_t *sd)
{
    sd_type_t type = SD_TYPE_NONE;
    plat_status_t st;
    plat_status_t end;
    unsigned i;

    if (sd == NULL) {
        return PLAT_ERR_ARG;
    }
    sd->type = SD_TYPE_NONE;

    st = plat_spi_set_speed(PLAT_SPI_SPEED_SLOW);
    plat_spi_cs(0U);
    for (i = 0U; st == PLAT_OK && i < POWER_UP_BYTES; i++) {
        st = xfer_byte(IDLE_BYTE, NULL);
    }
    if (st == PLAT_OK) {
        plat_spi_cs(1U);
        st = negotiate(&type);
    }
    end = release();
    if (st == PLAT_OK) {
        st = end;
    }
    if (st == PLAT_OK) {
        st = plat_spi_set_speed(PLAT_SPI_SPEED_FAST);
    }
    if (st == PLAT_OK) {
        sd->type = type;
    }
    return st;
}

static plat_status_t read_block_selected(uint32_t arg, uint8_t *buf)
{
    uint8_t r1 = 0U;
    uint8_t b = IDLE_BYTE;
    uint8_t crc[2];
    plat_status_t st = command(CMD17, arg, CRC_DUMMY, &r1);
    unsigned i;

    if (st != PLAT_OK) {
        return st;
    }
    if (r1 != 0U) {
        return PLAT_ERR_BUS;
    }
    for (i = 0U; i < SDCARD_TOKEN_POLLS; i++) {
        st = xfer_byte(IDLE_BYTE, &b);
        if (st != PLAT_OK) {
            return st;
        }
        if (b == TOKEN_START_BLOCK) {
            break;
        }
        if (b != IDLE_BYTE) {
            return PLAT_ERR_BUS;
        }
    }
    if (i == SDCARD_TOKEN_POLLS) {
        return PLAT_ERR_TIMEOUT;
    }
    st = read_bytes(buf, SDCARD_BLOCK_SIZE);
    if (st == PLAT_OK) {
        st = read_bytes(crc, sizeof crc);
    }
    return st;
}

plat_status_t sdcard_read_block(const sdcard_t *sd, uint32_t block, uint8_t *buf)
{
    uint32_t arg;
    plat_status_t st;
    plat_status_t end;

    if (sd == NULL || buf == NULL || sd->type == SD_TYPE_NONE) {
        return PLAT_ERR_ARG;
    }
    if (sd->type == SD_TYPE_V2_HC) {
        arg = block;
    } else {
        if (block > (UINT32_MAX / SDCARD_BLOCK_SIZE)) {
            return PLAT_ERR_ARG;
        }
        arg = block * SDCARD_BLOCK_SIZE;
    }

    plat_spi_cs(1U);
    st = read_block_selected(arg, buf);
    end = release();
    return (st != PLAT_OK) ? st : end;
}

int sdcard_boot_signature_ok(const uint8_t *block)
{
    if (block == NULL) {
        return 0;
    }
    return (block[SDCARD_BLOCK_SIZE - 2U] == 0x55U && block[SDCARD_BLOCK_SIZE - 1U] == 0xAAU) ? 1 : 0;
}
