#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>
#include "plat_status.h"


#define SDCARD_BLOCK_SIZE      512U
#define SDCARD_CMD0_TRIES      5U
#define SDCARD_R1_POLLS        8U
#define SDCARD_ACMD41_TRIES    1000U
#define SDCARD_TOKEN_POLLS     50000U

typedef enum {
    SD_TYPE_NONE = 0,
    SD_TYPE_V1,
    SD_TYPE_V2,
    SD_TYPE_V2_HC
} sd_type_t;

typedef struct {
    sd_type_t type;
} sdcard_t;

plat_status_t sdcard_init(sdcard_t *sd);

plat_status_t sdcard_read_block(const sdcard_t *sd, uint32_t block, uint8_t *buf);

int sdcard_boot_signature_ok(const uint8_t *block);

#endif /* SDCARD_H */
