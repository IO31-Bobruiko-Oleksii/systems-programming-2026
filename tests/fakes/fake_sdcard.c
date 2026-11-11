#include <string.h>
#include "fake_sdcard.h"
#include "plat_spi.h"

#define BLOCK  512U
#define OUT_CAPACITY  (BLOCK + 16U)

enum { WR_NONE, WR_WAIT_TOKEN, WR_DATA, WR_CRC };

static struct {
    uint8_t  silent;
    uint8_t  v1;
    uint8_t  high_capacity;
    int      busy_polls;
    uint32_t cmd8_echo;
    uint8_t  read_r1;
    uint8_t  read_token;
    uint8_t  write_r1;
    uint8_t  write_resp;
    unsigned write_busy_polls;
    uint32_t block_index;
    uint8_t  block_valid;
    uint8_t  block_data[BLOCK];
    long     fail_call;
    plat_status_t fail_status;
    plat_status_t init_status;

    uint8_t  spi_mode;
    uint8_t  idle;
    uint8_t  expect_acmd;
    uint8_t  hcs;
    int      busy_left;
    uint8_t  wr_state;
    size_t   wr_count;
    uint32_t wr_block;
    unsigned wr_busy;
    unsigned write_count;
    uint8_t  wr_buf[BLOCK];

    uint8_t  cs;
    plat_spi_speed_t speed;
    uint8_t  frame[6];
    uint8_t  frame_len;
    uint8_t  out[OUT_CAPACITY];
    size_t   out_head;
    size_t   out_tail;

    uint8_t  frames[FAKE_SD_FRAME_HISTORY][6];
    plat_spi_speed_t frame_speed[FAKE_SD_FRAME_HISTORY];
    size_t   cmd_count;
    unsigned idle_bytes;
    unsigned transfer_calls;
} m;

void fake_sd_reset(void)
{
    memset(&m, 0, sizeof m);
    m.high_capacity = 1U;
    m.cmd8_echo = 0x000001AAU;
    m.read_token = 0xFEU;
    m.write_resp = 0x05U;
    m.fail_call = -1;
    m.speed = PLAT_SPI_SPEED_SLOW;
}

void fake_sd_set_silent(uint8_t silent)            { m.silent = silent; }
void fake_sd_set_version1(uint8_t v1)              { m.v1 = v1; }
void fake_sd_set_high_capacity(uint8_t hc)         { m.high_capacity = hc; }
void fake_sd_set_acmd41_busy_polls(int polls)      { m.busy_polls = polls; }
void fake_sd_set_cmd8_echo(uint32_t value)         { m.cmd8_echo = value; }
void fake_sd_set_read_r1(uint8_t r1)               { m.read_r1 = r1; }
void fake_sd_set_read_token(uint8_t token)         { m.read_token = token; }
void fake_sd_set_write_r1(uint8_t r1)              { m.write_r1 = r1; }
void fake_sd_set_write_response(uint8_t token)     { m.write_resp = token; }
void fake_sd_set_write_busy_polls(unsigned polls)  { m.write_busy_polls = polls; }

void fake_sd_set_block(uint32_t index, const uint8_t *data512)
{
    m.block_index = index;
    m.block_valid = 1U;
    memcpy(m.block_data, data512, BLOCK);
}

void fake_sd_fail_transfer(unsigned call_index, plat_status_t status)
{
    m.fail_call = (long)call_index;
    m.fail_status = status;
}

size_t fake_sd_cmd_count(void)                     { return m.cmd_count; }
const uint8_t *fake_sd_frame_at(size_t index)      { return m.frames[index]; }
uint8_t fake_sd_cmd_at(size_t index)               { return (uint8_t)(m.frames[index][0] & 0x3FU); }
plat_spi_speed_t fake_sd_speed_at(size_t index)    { return m.frame_speed[index]; }
unsigned fake_sd_idle_bytes_before_first_cmd(void) { return m.idle_bytes; }
unsigned fake_sd_transfer_calls(void)              { return m.transfer_calls; }
unsigned fake_sd_write_count(void)                 { return m.write_count; }
uint32_t fake_sd_stored_index(void)                { return m.block_index; }
const uint8_t *fake_sd_stored_block(void)          { return m.block_data; }
uint8_t fake_sd_cs_asserted(void)                  { return m.cs; }
plat_spi_speed_t fake_sd_speed(void)               { return m.speed; }

uint32_t fake_sd_arg_at(size_t index)
{
    const uint8_t *f = m.frames[index];

    return ((uint32_t)f[1] << 24) | ((uint32_t)f[2] << 16) | ((uint32_t)f[3] << 8) | f[4];
}

static void push(uint8_t b)
{
    if (m.out_tail < OUT_CAPACITY) {
        m.out[m.out_tail++] = b;
    }
}

static void push_u32(uint32_t v)
{
    push((uint8_t)(v >> 24));
    push((uint8_t)(v >> 16));
    push((uint8_t)(v >> 8));
    push((uint8_t)v);
}

static void execute(void)
{
    uint8_t cmd = (uint8_t)(m.frame[0] & 0x3FU);
    uint8_t crc = m.frame[5];
    uint32_t arg = ((uint32_t)m.frame[1] << 24) | ((uint32_t)m.frame[2] << 16) |
                   ((uint32_t)m.frame[3] << 8) | m.frame[4];
    uint8_t acmd = m.expect_acmd;
    uint8_t not_idle = (uint8_t)(m.idle ? 0x01U : 0x00U);

    if (m.cmd_count < FAKE_SD_FRAME_HISTORY) {
        memcpy(m.frames[m.cmd_count], m.frame, sizeof m.frame);
        m.frame_speed[m.cmd_count] = m.speed;
    }
    m.cmd_count++;
    m.expect_acmd = 0U;
    m.out_head = 0U;
    m.out_tail = 0U;

    if (m.silent) {
        return;
    }
    if (cmd == 0U) {
        if (crc != 0x95U) {
            return;
        }
        m.spi_mode = 1U;
        m.idle = 1U;
        m.busy_left = m.busy_polls;
        push(0xFFU);
        push(0x01U);
        return;
    }
    if (!m.spi_mode) {
        return;
    }

    push(0xFFU);
    if (acmd && cmd == 41U) {
        m.hcs = (uint8_t)((arg >> 30) & 1U);
        if (m.busy_left == 0) {
            m.idle = 0U;
            push(0x00U);
        } else {
            if (m.busy_left > 0) {
                m.busy_left--;
            }
            push(0x01U);
        }
        return;
    }
    switch (cmd) {
    case 8U:
        if (m.v1) {
            push(0x05U);
        } else if (crc != 0x87U) {
            push(0x09U);
        } else {
            push(0x01U);
            push_u32(m.cmd8_echo);
        }
        break;
    case 55U:
        m.expect_acmd = 1U;
        push(not_idle);
        break;
    case 58U:
        push(not_idle);
        push_u32(((m.idle ? 0U : 1U) << 31) |
                 ((!m.idle && m.high_capacity && m.hcs) ? 0x40000000U : 0U) | 0x00FF8000U);
        break;
    case 17U: {
        uint32_t block = m.high_capacity ? arg : arg / BLOCK;

        if (m.idle) {
            push(0x05U);
        } else if (m.read_r1 != 0U) {
            push(m.read_r1);
        } else {
            push(0x00U);
            push(0xFFU);
            push(0xFFU);
            if (m.read_token == 0xFFU) {
                break;
            }
            push(m.read_token);
            if (m.read_token == 0xFEU) {
                size_t i;

                for (i = 0U; i < BLOCK; i++) {
                    push((m.block_valid && block == m.block_index) ? m.block_data[i] : 0x00U);
                }
                push(0xFFU);
                push(0xFFU);
            }
        }
        break;
    }
    case 24U: {
        uint32_t block = m.high_capacity ? arg : arg / BLOCK;

        if (m.idle) {
            push(0x05U);
        } else if (m.write_r1 != 0U) {
            push(m.write_r1);
        } else {
            push(0x00U);
            m.wr_state = WR_WAIT_TOKEN;
            m.wr_count = 0U;
            m.wr_block = block;
        }
        break;
    }
    default:
        push(m.idle ? 0x05U : 0x04U);
        break;
    }
}

static void finish_write(void)
{
    m.wr_state = WR_NONE;
    m.out_head = 0U;
    m.out_tail = 0U;
    push(m.write_resp);
    m.wr_busy = m.write_busy_polls;
    if ((m.write_resp & 0x1FU) == 0x05U) {
        memcpy(m.block_data, m.wr_buf, BLOCK);
        m.block_index = m.wr_block;
        m.block_valid = 1U;
        m.write_count++;
    }
}

static uint8_t write_byte(uint8_t in)
{
    uint8_t out = 0xFFU;

    switch (m.wr_state) {
    case WR_WAIT_TOKEN:
        if (in == 0xFEU) {
            m.wr_state = WR_DATA;
            m.wr_count = 0U;
        } else if (m.out_head < m.out_tail) {
            out = m.out[m.out_head++];
        }
        break;
    case WR_DATA:
        m.wr_buf[m.wr_count++] = in;
        if (m.wr_count == BLOCK) {
            m.wr_state = WR_CRC;
            m.wr_count = 0U;
        }
        break;
    default:
        if (++m.wr_count == 2U) {
            finish_write();
        }
        break;
    }
    return out;
}

static uint8_t card_byte(uint8_t in)
{
    uint8_t out = 0xFFU;

    if (!m.cs) {
        if (m.cmd_count == 0U) {
            m.idle_bytes++;
        }
        return 0xFFU; 
    }
    if (m.wr_state != WR_NONE) {
        return write_byte(in);
    }
    if (m.frame_len == 0U && (in & 0xC0U) != 0x40U) {
        if (m.out_head < m.out_tail) {
            out = m.out[m.out_head++];
        } else if (m.wr_busy > 0U) {
            m.wr_busy--;
            out = 0x00U;
        }
        return out;
    }
    m.frame[m.frame_len++] = in;
    if (m.frame_len == 6U) {
        m.frame_len = 0U;
        execute();
    }
    return 0xFFU;
}

void fake_sd_fail_spi_init(plat_status_t status)
{
    m.init_status = status;
}

plat_status_t plat_spi_init(void)
{
    m.cs = 0U;
    m.speed = PLAT_SPI_SPEED_SLOW;
    return m.init_status;
}

plat_status_t plat_spi_set_speed(plat_spi_speed_t speed)
{
    m.speed = speed;
    return PLAT_OK;
}

void plat_spi_cs(uint8_t asserted)
{
    if (asserted == 0U) {
        m.wr_state = WR_NONE;
        m.wr_busy = 0U;
        m.frame_len = 0U;
        m.out_head = 0U; 
        m.out_tail = 0U;
    }
    m.cs = (uint8_t)(asserted != 0U);
}

plat_status_t plat_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t i;
    unsigned call = m.transfer_calls++;

    if (len == 0U) {
        return PLAT_ERR_ARG;
    }
    if (m.fail_call >= 0 && (long)call == m.fail_call) {
        return m.fail_status;
    }
    for (i = 0U; i < len; i++) {
        uint8_t out = card_byte(tx != NULL ? tx[i] : 0xFFU);

        if (rx != NULL) {
            rx[i] = out;
        }
    }
    return PLAT_OK;
}
