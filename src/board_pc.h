/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * board_pc.h — ClockworkPi PicoCalc (mainboard v2.0) with a Raspberry Pi
 * Pico 2 or Pico 2 W (RP2350A) in its socket.
 *
 * Display: a 320x320 ST7365P SPI LCD on SPI1.  No HDMI, no VGA, no composite.
 * Input: an STM32 keyboard controller on I2C1 at 0x1F.  No PS/2, no NES pad.
 * Audio: PWM on GPIO 26/27, which are both halves of PWM slice 5, through a
 * filter to the speakers and the headphone jack.  No I2S DAC, no HDMI audio.
 * Console: UART0 on GPIO 0/1, which reaches the PicoCalc's USB-C port through
 * a CH340C.
 *
 * Every pin was read from the mainboard v2.0 schematic's Pico-socket net
 * labels, and agrees with ClockworkPi's sample code.  See docs/PICOCALC.md
 * section 2.1.
 *
 * Pins this board must never touch:
 *   GPIO 23, 24, 25, 29  internal to the Pico module, and on a Pico 2 W they
 *                        belong to the CYW43 wireless chip (25 is its chip
 *                        select, not an LED).  One UF2 runs on both modules.
 *   GPIO 2-5, 20, 21     the board's 8 MB PSRAM, which is not used.
 *   GPIO 8, 9            UART1, wired to the keyboard STM32's UART3.
 *
 * Selected when -DPLATFORM=pc.
 */
#ifndef BOARD_PC_H
#define BOARD_PC_H

/* ---- Capabilities ----
 * Only PWM audio.  Deliberately no HAS_PS2, HAS_NESPAD, HAS_I2S, HAS_TV or
 * HAS_HDMI_AUDIO: none of that hardware exists on this board, and the absence
 * of the macros is what gates the code out. */
#define HAS_PWM 1

/* ---- LCD: ST7365P, 320x320, on SPI1 ---- */
#define LCD_SPI_PORT  spi1
#define LCD_PIN_SCK   10
#define LCD_PIN_MOSI  11
#define LCD_PIN_MISO  12
#define LCD_PIN_CS    13
#define LCD_PIN_DC    14
#define LCD_PIN_RST   15
/* The panel's TE (tearing effect) pin is not connected to the Pico. */

/* ---- SD card (hardware SPI0) ----
 * These four values are also set from CMakeLists.txt as compile definitions,
 * and that copy is the one drivers/sdcard/sdcard.c actually sees.  Both must
 * agree. */
#define SDCARD_PIN_SPI0_SCK  18
#define SDCARD_PIN_SPI0_MOSI 19
#define SDCARD_PIN_SPI0_MISO 16
#define SDCARD_PIN_SPI0_CS   17
/* Card detect, low when a card is present.  The existing SD_DETECT_PIN block
 * in main() enables the pull-up. */
#define SD_DETECT_PIN        22

/* ---- Keyboard controller: STM32 on I2C1, 7-bit address 0x1F ---- */
#define KBD_I2C_PORT  i2c1
#define KBD_PIN_SDA   6
#define KBD_PIN_SCL   7
#define KBD_I2C_ADDR  0x1F

/* ---- Console: UART0, through the CH340C to the USB-C port ----
 * Driven by drivers/uart_stdio_nb.c, not pico_stdio_uart. */
#define CONSOLE_UART   uart0
#define CONSOLE_TX_PIN 0
#define CONSOLE_RX_PIN 1
#define CONSOLE_BAUD   115200

/* ---- PWM audio: slice 5, A = left, B = right ---- */
#define PWM_PIN0 26
#define PWM_PIN1 27

/* ---- I2S: no output on this board ----
 * drivers/audio.c and frank_audio.c need these symbols to compile.  The I2S
 * backend is never initialised on this platform: frank_settings.c offers PWM
 * only, and frank_audio_set_driver() folds every other choice onto PWM.  They
 * name the PWM pins so that nothing else on the board could be driven even if
 * that ever changed. */
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 26

/* ---- PSRAM: present but not used ----
 * No PSRAM_CS_PIN_RP2350A here, and that is not enough on its own:
 * board_config.h defaults the pin to 0, which on this board is UART0 TX.  The
 * probe is compiled out in main() under PLATFORM_PC instead. */

#endif /* BOARD_PC_H */
