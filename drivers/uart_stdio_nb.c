/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * uart_stdio_nb.c — non-blocking stdio over a PL011 UART.  See the header.
 *
 * TX: out_chars() copies into a ring and moves as much as fits straight into
 * the hardware FIFO.  If anything is left, the TX interrupt is enabled and
 * drains the rest as the FIFO empties.  The PL011's TX interrupt fires on the
 * FIFO level crossing its threshold, not on the level being below it, which
 * is why the FIFO is primed directly rather than by enabling the interrupt
 * and waiting for it.
 *
 * RX: the receive and receive-timeout interrupts move bytes into a small
 * ring, which in_chars() serves without waiting.  That is what the serial
 * console's getchar_timeout_us(0) reads.
 *
 * The rings are shared between thread code on either core and the UART
 * interrupt on the core that called init, so every access to the indices is
 * under a critical section (interrupts off on this core plus a spinlock).
 * It is held only while bytes are copied between memory and the FIFO, never
 * while waiting on the UART: a microsecond or two for a typical printf.  So
 * critical_section_enter_blocking() is the one "blocking" call here, and what
 * it can wait for is another core's copy, not the wire.
 *
 * CR/LF: the SDK's stdio layer translates \n to \r\n before calling
 * out_chars() when crlf_enabled is set, so this driver does not.
 */
#include <string.h>

#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/critical_section.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "uart_stdio_nb.h"

#define TX_RING_SIZE 2048u   /* power of two */
#define RX_RING_SIZE 64u     /* power of two */

static uart_inst_t        *s_uart;
static critical_section_t  s_lock;

static uint8_t             s_tx[TX_RING_SIZE];
static volatile uint32_t   s_tx_head, s_tx_tail;    /* head: write, tail: read */
static uint8_t             s_rx[RX_RING_SIZE];
static volatile uint32_t   s_rx_head, s_rx_tail;
static volatile uint32_t   s_dropped;

/* Move ring bytes into the hardware FIFO until one is empty or the other is
 * full, then enable the TX interrupt only if bytes remain.  Caller holds
 * s_lock or is the interrupt handler on the owning core with s_lock held. */
static void tx_pump_locked(void) {
    uart_hw_t *hw = uart_get_hw(s_uart);
    while (s_tx_tail != s_tx_head && !(hw->fr & UART_UARTFR_TXFF_BITS)) {
        hw->dr = s_tx[s_tx_tail & (TX_RING_SIZE - 1)];
        s_tx_tail++;
    }
    if (s_tx_tail != s_tx_head)
        hw_set_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
    else
        hw_clear_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
}

static void uart_nb_irq(void) {
    uart_hw_t *hw = uart_get_hw(s_uart);
    critical_section_enter_blocking(&s_lock);
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
        uint8_t c = (uint8_t)hw->dr;
        if (s_rx_head - s_rx_tail < RX_RING_SIZE) {
            s_rx[s_rx_head & (RX_RING_SIZE - 1)] = c;
            s_rx_head++;
        }
    }
    tx_pump_locked();
    critical_section_exit(&s_lock);
}

static void nb_out_chars(const char *buf, int len) {
    critical_section_enter_blocking(&s_lock);
    for (int i = 0; i < len; i++) {
        if (s_tx_head - s_tx_tail >= TX_RING_SIZE) {
            s_dropped += (uint32_t)(len - i);
            break;
        }
        s_tx[s_tx_head & (TX_RING_SIZE - 1)] = (uint8_t)buf[i];
        s_tx_head++;
    }
    tx_pump_locked();
    critical_section_exit(&s_lock);
}

static int nb_in_chars(char *buf, int len) {
    int n = 0;
    critical_section_enter_blocking(&s_lock);
    while (n < len && s_rx_tail != s_rx_head) {
        buf[n++] = (char)s_rx[s_rx_tail & (RX_RING_SIZE - 1)];
        s_rx_tail++;
    }
    critical_section_exit(&s_lock);
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t s_driver = {
    .out_chars = nb_out_chars,
    .in_chars  = nb_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

void uart_stdio_nb_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint baud) {
    s_uart = uart;
    critical_section_init(&s_lock);

    uart_init(uart, baud);                      /* also enables the FIFOs */
    gpio_set_function(tx_pin, UART_FUNCSEL_NUM(uart, tx_pin));
    gpio_set_function(rx_pin, UART_FUNCSEL_NUM(uart, rx_pin));

    uart_hw_t *hw = uart_get_hw(uart);
    /* Interrupt when the TX FIFO drains to 1/8 full and when the RX FIFO
     * reaches 1/8; the receive timeout catches the stragglers below that. */
    hw->ifls = (0u << UART_UARTIFLS_TXIFLSEL_LSB) | (0u << UART_UARTIFLS_RXIFLSEL_LSB);
    hw->imsc = UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS;

    uint irq = UART_IRQ_NUM(uart);
    irq_set_exclusive_handler(irq, uart_nb_irq);
    irq_set_enabled(irq, true);

    stdio_set_driver_enabled(&s_driver, true);
}

uint32_t uart_stdio_nb_dropped(void) { return s_dropped; }
