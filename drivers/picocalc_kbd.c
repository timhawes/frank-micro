/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * picocalc_kbd.c — the PicoCalc keyboard controller's event FIFO, read over
 * I2C without ever blocking.
 *
 * The keyboard is an STM32 at 0x1F on i2c1.  Writing register 0x09 dequeues
 * one event, and the two-byte read that follows returns it as
 * [state, code], or [0, 0] when the queue is empty.
 *
 * ClockworkPi's sample sleeps 16 ms between the write and the read.  This
 * driver cannot sleep: it runs on core 0, inside the emulator's frame.  It
 * relies instead on the RP2350 I2C block, which works through its command
 * FIFO by itself and starts a new transaction after each STOP.  Each poll
 * collects the bytes the previous poll's reads left in the RX FIFO, then
 * queues the next batch and returns.  No gap is needed between the write and
 * the read; that was checked on hardware (docs/HANDOFF.md).
 *
 * At the sample's 10 kHz, one transaction takes about 5 ms.  Three a frame is
 * about 15 ms of bus time, run entirely by the hardware, and 150 events a
 * second, against about 10 a second from a held key.
 *
 * Nothing here writes to any controller register.  The controller is also the
 * power manager; register 0x08 resets it and 0x0E powers the board off.
 */
#include "picocalc_kbd.h"

#include <stdbool.h>
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "board_config.h"

#define KBD_I2C_HZ      10000u  /* ClockworkPi's own value; see the header note */
#define KBD_REG_FIFO    0x09u
#define KBD_BATCH       3       /* transactions queued per poll */
#define KBD_STALL_POLLS 10      /* a batch still unfinished after this is reset */

static uint32_t s_errors;
static bool     s_outstanding;
static int      s_waited;
static bool     s_draining;     /* discard events until the queue is seen empty */

static void kbd_bus_init(void) {
    i2c_init(KBD_I2C_PORT, KBD_I2C_HZ);
    gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_PIN_SDA);
    gpio_pull_up(KBD_PIN_SCL);

    /* The target address is fixed, so set it once rather than per call as the
     * SDK's blocking functions do. */
    i2c_hw_t *hw = i2c_get_hw(KBD_I2C_PORT);
    hw->enable = 0;
    hw->tar = KBD_I2C_ADDR;
    hw->enable = 1;
    s_outstanding = false;
    s_waited = 0;
}

void picocalc_kbd_init(void) {
    kbd_bus_init();
    s_draining = true;
}

uint32_t picocalc_kbd_errors(void) { return s_errors; }

static void kbd_queue_batch(i2c_hw_t *hw) {
    for (int i = 0; i < KBD_BATCH; i++) {
        hw->data_cmd = KBD_REG_FIFO | I2C_IC_DATA_CMD_STOP_BITS;
        hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS;
        hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_STOP_BITS;
    }
    s_outstanding = true;
    s_waited = 0;
}

void picocalc_kbd_poll(picocalc_kbd_event_fn fn) {
    i2c_hw_t *hw = i2c_get_hw(KBD_I2C_PORT);

    if (s_outstanding) {
        if (hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
            /* A NAK or lost arbitration.  The block has flushed its TX FIFO;
             * reading clr_tx_abrt releases it.  Drop any partial reply. */
            (void)hw->clr_tx_abrt;
            while (hw->rxflr) (void)hw->data_cmd;
            s_errors++;
            s_outstanding = false;
        } else if (hw->rxflr < 2u * KBD_BATCH) {
            /* Still on the bus.  If it never finishes, start the block again. */
            if (++s_waited < KBD_STALL_POLLS) return;
            s_errors++;
            kbd_bus_init();
        } else {
            uint8_t rx[2 * KBD_BATCH];
            for (int i = 0; i < 2 * KBD_BATCH; i++) rx[i] = (uint8_t)hw->data_cmd;
            s_outstanding = false;
            /* An empty pair can sit between two events if a key arrived
             * mid-batch, so every pair is looked at.  Skipping one would
             * lose a release and leave a key stuck. */
            for (int i = 0; i < KBD_BATCH; i++) {
                uint8_t state = rx[2 * i], code = rx[2 * i + 1];
                if (state == 0 && code == 0) {
                    s_draining = false;
                    continue;
                }
                if (!s_draining && fn) fn(state, code);
            }
        }
    }

    if (!s_outstanding) kbd_queue_batch(hw);
}
