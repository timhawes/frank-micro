/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * uart_stdio_nb.h — a stdio driver on a UART that never blocks the caller.
 *
 * The SDK's stdio_uart writes with uart_putc(), which spins while the 32-byte
 * TX FIFO is full: about 87 us a character at 115200 baud, so a 100-character
 * line can stall core 0 for several milliseconds.  This driver queues output
 * in a RAM ring that the UART's TX interrupt drains, and drops characters
 * (counting them) when the ring is full, so printf() costs a memcpy.
 */
#ifndef UART_STDIO_NB_H
#define UART_STDIO_NB_H

#include <stdint.h>
#include "hardware/uart.h"

/* Initialise the UART and its pins, then register the driver with stdio.
 * Call after stdio_init_all(). */
void uart_stdio_nb_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint baud);

/* Characters dropped because the TX ring was full, since boot. */
uint32_t uart_stdio_nb_dropped(void);

#endif /* UART_STDIO_NB_H */
