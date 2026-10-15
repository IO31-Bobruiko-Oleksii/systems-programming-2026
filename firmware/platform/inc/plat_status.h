#ifndef PLAT_STATUS_H
#define PLAT_STATUS_H

typedef enum {
    PLAT_OK = 0,
    PLAT_ERR_ARG,
    PLAT_ERR_TIMEOUT,
    PLAT_ERR_BUS
} plat_status_t;

#endif /* PLAT_STATUS_H */
