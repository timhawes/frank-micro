# Adding the ClockworkPi PicoCalc as a target

Date: 2026-10-30
Scope: what has to change in `frank-micro` to build and run on a ClockworkPi
PicoCalc (mainboard v2.0) fitted with a Raspberry Pi Pico 2 or Pico 2 W, as a
new board `picocalc`.
Method: the PicoCalc side was read from ClockworkPi's own repository,
`clockworkpi/PicoCalc` at commit `fdcfe2c`. That covers the mainboard v2.0
schematic (PDF, text-extracted), the ST7365P datasheet, the keyboard MCU
firmware source and the `picocalc_helloworld` sample. The Pico 2 W side was
read from Raspberry Pi's Pico 2 W datasheet. The frank-micro side was read
statically at `fruit-jam-port` commit `d8d25f5`. No PicoCalc hardware was
tested, and no firmware code has been written.

The brief is `REQUIREMENTS.md` at the repository root. This document is the
Phase 0 deliverable it asks for. Claims are rated in section 12. Anything that
needs the board is marked **needs hardware**.

---

## 1. Summary

The PicoCalc is a different kind of target from the other four boards. They
all drive a monitor from PIO or HSTX at a fixed refresh rate. The PicoCalc has
a 320×320 SPI LCD that keeps its own frame memory, so the scanout stops being
a real-time obligation and becomes a bandwidth problem. Input is not PS/2 or
USB: it comes from an STM32 keyboard controller over I²C, which reports
characters, not key positions.

Most of the platform maps onto what exists:

- **SD card**: hardware SPI0 with three pins plus a chip select, the shape
  `drivers/sdcard/sdcard.c` already expects. Pins only.
- **Audio**: both speaker channels are on one PWM slice, which
  `drivers/pwm_audio/pwm_audio.c` already supports (`stereo_same_slice`). Pins
  only, plus the non-blocking fix in section 5.
- **Framebuffer**: the 320×256 8bpp double buffer and `blit_row()` stay as
  they are. The LCD is 320 wide, so the PIO-path point-sampling carries over
  unchanged.
- **Console**: `uart0` on GPIO 0 and 1, which are also the SDK `pico2` board
  defaults.

Three parts are new:

| Part | Where | Size |
|---|---|---|
| LCD scanout on core 1, implementing the `graphics_*` interface in `drivers/HDMI.h` | `drivers/lcd_st7365p.c` | ~400 lines |
| Keyboard: a non-blocking I²C FIFO reader, and a PicoCalc-to-BBC layer that handles the firmware's shifted codes | `drivers/picocalc_kbd.c`, `src/frank/frank_keyboard.c` | ~350 lines |
| A UART stdio driver that never blocks core 0 | `drivers/uart_stdio_nb.c` | ~120 lines |

Plus `src/board_picocalc.h`, a `board_config.h` branch, CMake and `build.sh`.

Findings that change the brief:

1. **The keyboard's config register cannot be written.** The brief planned to
   clear `CFG_USE_MODS` at boot so that keys would report raw positions. The
   shipped firmware (version `0x16`) has no write handler for that register,
   so Shift always changes the reported code. The key map in section 7 is
   designed around that. See section 5.2.
2. **There is no tearing signal.** The LCD connector's `TE` pin has no net on
   the mainboard schematic. Expect tearing on fast-moving screens. See
   section 5.1.
3. **The PSRAM probe would take the console pin.** With no PSRAM pin declared,
   `board_config.h` defaults it to GPIO 0, which on this board is UART0 TX. The
   probe has to be compiled out on `picocalc`, not just left at its default.
4. **Ten keys are layers, not keys.** F6–F10, BREAK, HOME, END, PgUp, PgDn and
   Insert exist only as the Shift layer of another key.

Effort, by the phases in section 8:

| Phase | Deliverable | Risk |
|---|---|---|
| 1 | Boots blind: UART0 console, SD, emulator at 100%, status line | Low. Routine board bring-up |
| 2 | First picture on the LCD, full-frame push | Low to medium. Panel init order is the unknown |
| 3 | Keyboard, the agreed key map and menu chords | Medium. The I²C write/read gap and the firmware version on the unit |
| 4 | Sound through the speakers | Low. Making the PWM push non-blocking is the work |
| 5 | Speed: changed rows only, DMA, faster SPI | The main risk in the port: how fast the panel accepts SPI |
| 6 | Polish: status line, battery, release, README | Low |

---

## 2. The board

The PicoCalc mainboard v2.0 takes a Pico-footprint module in a socket. It has:

- an ST7365P 320×320 IPS LCD on SPI1
- an SD socket on SPI0
- an STM32 that scans the keyboard matrix and also manages power. It owns an
  AXP2101 PMU, controls the Pico's power enable (`PICO_EN`), the LCD backlight
  and keyboard backlight, the speaker amplifier enable (`PA_EN`) and the
  headphone detect (`HP_DET`)
- two speakers and a headphone jack driven from PWM through a filter
  (`PCM_AUDIO_L/R`)
- 8 MB of SPI PSRAM on PIO pins
- a USB-C port behind a CH340C USB-serial bridge and two WAS7227 USB switches

The target module is a **Pico 2 (RP2350A, 520 KB SRAM, 4 MB flash) or a Pico 2
W**. An RP2040 Pico is out of scope. See section 2.2 for the W.

### 2.1 Verified pin assignments

Every row of the brief's pin table was checked against the schematic's
Pico-socket net labels (`GPn NET_NAME`). Every row agrees.

| Function | Peripheral | GPIO | Schematic net | Brief's source | Status |
|---|---|---|---|---|---|
| LCD SCK, MOSI, MISO | `spi1` | 10, 11, 12 | `SPI1_SCK`, `SPI1_TX`, `SPI1_RX` | `lcdspi.h` | Confirmed, read |
| LCD CS, DC, RST | GPIO | 13, 14, 15 | `SPI1_CS`, `LCD_DC`, `LCD_RST` | `lcdspi.h` | Confirmed, read |
| LCD TE | none | — | `TE` pin on the LCD connector carries no net | — | **Not connected**, read; confirm visually |
| SD SCK, MOSI, MISO, CS | `spi0` | 18, 19, 16, 17 | `SPI0_SCK`, `SPI0_TX`, `SPI0_RX`, `SPI0_CS` | `sd_boot/config.h` | Confirmed, read |
| SD card detect, active low | GPIO, pull-up | 22 | `SD_DET` | `sd_boot/config.h` | Confirmed, read. Polarity from the sample, not the schematic |
| Keyboard SDA, SCL | `i2c1`, 0x1F | 6, 7 | `I2C1_SDA`, `I2C1_SCL` → STM32 `M_I2C1` | `i2ckbd.h` | Confirmed, read. Address from firmware `conf_app.h` `SLAVE_ADDRESS 0x1F` |
| Audio left, right | PWM slice 5 A, B | 26, 27 | `PWM_L`, `PWM_R` | `pwm_sound` | Confirmed, read. Slice from the RP2350 GPIO table: GPIO 26 = 5A, 27 = 5B |
| Console | `uart0` | 0 TX, 1 RX | `UART0_TX`, `UART0_RX` → USB-C mux | schematic | Confirmed, read |
| Link to the STM32 | `uart1` | 8 TX, 9 RX | `UART1_TX` → `M_UART3_RX`, `UART1_RX` → `M_UART3_TX` | — | **New**, read. Not in the brief. Unused here |
| PSRAM, not used | PIO | CS 20, SCK 21, IO0–IO3 2, 3, 4, 5 | `RAM_CS`, `RAM_SCK`, `RAM_TX`, `RAM_RX`, `RAM_IO2`, `RAM_IO3` | helloworld `CMakeLists.txt` | Confirmed. The brief listed only 2 and 3; 4 and 5 are its quad lines |
| Spare | — | 28 | `GP28`, to an expansion header | — | Free |

GPIO 23, 24, 25 and 29 do not appear on the mainboard at all. They are
internal to the Pico module and never reach its edge pads.

### 2.2 Pico 2 and Pico 2 W

The test unit is a **Pico 2 W**, and the firmware must run on both. Four GPIOs
differ between the two modules (Pico 2 W datasheet, section "Pinout"):

| GPIO | Pico 2 | Pico 2 W |
|---|---|---|
| 23 | SMPS power-save | CYW43 `WL_ON`, the wireless power-on |
| 24 | VBUS sense | CYW43 SPI data and IRQ |
| 25 | User LED | CYW43 SPI chip select. When high, it also enables the GPIO 29 VSYS ADC |
| 29 | VSYS/3 on ADC3 | CYW43 SPI clock, and VSYS/3 on ADC3 |

The plan is one UF2, built with `PICO_BOARD=pico2`, that never touches any of
the four:

- No PicoCalc function uses them (section 2.1).
- `main()` in `src/frank/frank_platform.c`, in its `PICO_DEFAULT_LED_PIN`
  block, drives GPIO 25 high. On a W that is the CYW43 chip select. Phase 1
  gates the block out on `picocalc`. The other boards' preprocessor output is
  unchanged.
- `drivers/test_pins.c` also reads `PICO_DEFAULT_LED_PIN`, but it is not built
  on `picocalc` because there is no VGA detection.
- The CYW43 stays powered off because nothing drives `WL_ON`. This is
  **derived**: RP2350 bank-0 pads come out of reset with the pull-down on and
  input isolation set, so GPIO 23 sits low. **Needs hardware**: current draw
  with and without a W is the cheap check.
- No `pico_cyw43_arch`. Wireless stays out of scope, as the brief says.
- Battery level comes from the keyboard MCU over I²C, never from the VSYS ADC
  on GPIO 29.

Nothing needs to tell a W from a non-W at runtime. Hardware results record
which module they came from. Until someone runs the non-W, it is untested.

### 2.3 The USB-C port and the console

The USB-C connector feeds U703, a WAS7227 USB switch. One side goes to the
STM32's own USB (`M_USB_DP/DM`). The other goes to U702, a CH340C USB-serial
bridge, whose serial side passes through U701, a second WAS7227, to either the
Pico's UART0 or the STM32's UART1. Both switches share the `SEL1`/`SEL2`
lines, and `SW701` (an EM-02KP two-way slide switch) appears to set them.

What that means here, **derived from part numbers and net names**: with the
switch in the right position, the PicoCalc's own USB-C port is a serial
console for UART0, with no wiring. **Needs hardware**: which switch position
that is. The Pico module's own USB connector is a fallback, but whether it can
be reached inside the case is for you to check.

---

## 3. What needs no new code

### SD card on the existing SPI0 path

`sdcard.c` takes its pins from CMake compile definitions
(`SDCARD_PIN_SPI0_*`), and `spi0` is its default bus. GPIO 16/17/18/19 are
SPI0 RX/CSn/SCK/TX in the RP2350 function table, so the pins are valid for the
peripheral. Card detect on GPIO 22 reuses the `SD_DETECT_PIN` block in
`main()`, which already sets the pull-up.

`CLK_FAST` is 30 MHz, so at the default `clk_peri` of 48 MHz the card runs at
24 MHz. If `clk_peri` is moved to `clk_sys` (section 6), `spi_set_baudrate()`
recomputes the divider and gets 25.2 MHz, provided the move happens before
`sd_init`. The Fruit Jam learned this order the hard way (`docs/HANDOFF.md`,
2026-08-26).

### PWM audio, stereo on one slice

GPIO 26 and 27 are slice 5 A and B, so one slice's packed CC register
drives both. Since Phase 4 the PicoCalc does not use `pwm_audio.c`: its
blocking push let the audio clock pace the emulator. It uses
`drivers/pwm_ring_audio.c` instead, a 4,096-sample ring played by DMA
channels 8 and 9 in a loop with no interrupt. The producer never waits, and
the ring absorbs the producer's small rate error by skipping or repeating at
most one sample per block (see `docs/HANDOFF.md`). Nothing else on
`picocalc` uses channels 8 and 9. The amplifier enable `PA_EN` belongs to the STM32, which holds it
high at boot (`picocalc_keyboard.ino`, `setup()`), so the Pico has nothing to
switch.

### The framebuffer, the blit, the overlays

Everything up to the 320×256 double buffer is unchanged: `blit_row()`, the
MODE 7 renderer, `frank_ui_render()` compositing the F11/F12 overlays into the
completed buffer, and the present call
`graphics_set_buffer(SCREEN[current_buffer])` at the end of
`x_gui_end_scanline()`. That present call is the LCD driver's frame signal.
Because the overlay is composited before the present, the HSTX build's per-row
overlay problem (`CLAUDE.md` section 6) does not arise.

### The key-event pipeline

`process_xt_event()` in `frank_keyboard.c` already does the parts that are not
specific to an input device: Ctrl+Alt+Del, the overlay toggles, routing keys to
an open overlay, and the release-falls-through rule that stops keys sticking.
The PicoCalc layer reuses that logic. Section 5.2 says how.

---

## 4. What has no equivalent on this board

| Thing | Consequence |
|---|---|
| HDMI, VGA, composite | No PIO video. Every `HDMI_DRIVER` value except the new `LCD_SPI` is rejected for `picocalc` at configure time |
| HDMI-embedded audio, I2S | The F12 audio menu offers PWM only. A `micro.ini` written on another board with `audio_driver` HDMI or I2S is coerced to PWM, as `frank_settings.c` already does for HDMI in the `HDMI_PIO` builds |
| PS/2, NES pad | `board_picocalc.h` omits `HAS_PS2` and `HAS_NESPAD`, as `board_fj.h` does |
| USB HID host | `USB_HID=1` is rejected for `picocalc`. The native USB port is the Pico module's own connector, not a host socket |
| PSRAM | Present (8 MB on PIO) but not used, per the brief's N6. Compiled out, not probed. See section 9 |

---

## 5. The new parts

### 5.1 The LCD scanout

**The panel.** Sitronix ST7365P, 320×320. ClockworkPi's sample drives it with
ILI9488 command names (`lcdspi.c`, `pico_lcd_init()`): sleep out, `MADCTL` =
MX|BGR (0x48), pixel format 0x66 (18-bit), `INVON`, display on. Facts from the
datasheet (`ST7365P_SPEC_V1.0.pdf`):

- **16-bit colour over 4-line SPI.** Section 8.4.2.3 shows 65K colour, RGB
  5-6-5, two bytes per pixel. That section writes it as `3Ah = 05h`, and
  section 8.3 as `3Ah = 55h`. The low nibble selects the MCU-interface format
  and the high nibble the RGB-interface format, so both values give 16 bits
  over SPI. The brief's `55h` is fine. This driver uses `55h`.
- **Rated write clock.** `TSCYCW`, serial clock cycle (write), minimum 66 ns,
  so about 15 MHz. The sample asks for 25 MHz, but at the default
  `clk_peri` of 48 MHz the SDK can only give it 24. Anything above 15 MHz is
  outside the datasheet but in common use, so the clock is a tunable.
- **Tearing effect.** The controller has `TEON` (35h), but `TE` is not wired
  to the Pico (section 2.1), so the driver cannot sync to the panel's scan.

**Bandwidth.** A full 320×256 RGB565 frame is 163,840 bytes, or 1,310,720
bits:

| SPI clock | How | Full frame | Full frames per second |
|---|---|---|---|
| 24 MHz | `clk_peri` 48 MHz ÷ 2 | 54.6 ms | 18 |
| 31.5 MHz | `clk_peri` = `clk_sys` 252 ÷ 8 | 41.6 ms | 24 |
| 42 MHz | 252 ÷ 6 | 31.2 ms | 32 |
| 63 MHz | 252 ÷ 4 | 20.8 ms | 48 |

Derived figures, ignoring command overhead. Whether the panel holds a clean
picture above 24 MHz **needs hardware** (Phase 5). So full frames alone cannot
meet N2 (25 fps on typical screens) at the default clock, and changed-row
tracking is the main lever.

**Design**, following the brief's architecture section:

1. `graphics_init()` brings up SPI1, resets and initialises the panel, clears
   all 320 rows to black, and sets the column window to 0–319 once.
   `graphics_set_buffer(p)` is the present: it stores `p` and a frame
   sequence number for core 1. It is a pointer store and a counter increment,
   so core 0 never waits.
2. Core 1 runs `lcd_core1_run()`. It waits for a new sequence number, then
   walks the 256 rows of the latched buffer. For each row it computes a 32-bit
   checksum and compares it with `row_sum[256]`, held in RAM (1 KB, against
   80 KB for a shadow copy).
3. Changed rows are collected into runs. For each run it sets the page window
   to rows `32 + y0 … 32 + y1` (the frame is centred, with 32 rows above and
   below), sends `RAMWR`, then for each row converts 320 indices through a
   256-entry RGB565 table into one of two 640-byte line buffers. DMA feeds
   SPI1 in 16-bit frames from one buffer while the CPU fills the other.
4. `graphics_set_palette()` updates the RGB565 table and clears `row_sum[]`,
   so the next frame resends everything.
5. **As built in Phase 5, core 1 only ever sends complete frames.** Core 0
   no longer flips buffers blindly: at the start of each frame
   `lcd_begin_frame()` gives it the buffer core 1 is *not* sending, under a
   hardware spin lock held for a few instructions. Frames core 1 cannot keep
   up with are skipped whole. The Phase 0 design accepted torn frames here;
   on the board they showed as tearing on whole-screen motion, and fixing it
   cost no RAM. Core 0 still never waits for the LCD.
7. **Writes are timed to the panel's own refresh.** `TE` is not wired, but
   MISO is, and `RDSCANLINE` (`45h`) reads back which line the panel is
   scanning. Core 1 measures the refresh at boot (484 lines in about
   17,040 µs) and, before each send, waits only if the scan would otherwise
   cross the rows being written. See `lcd_wait_scan()` for the arithmetic.
6. The 64 spare rows (0–31 and 288–319) belong to the driver. Phase 6 draws
   the status line there (F11).

DMA channels are claimed with `dma_claim_unused_channel()`, **after**
`pwm_audio_init()` has run, or with 8–11 reserved first. PWM hardcodes 8 and
9. Its claim would otherwise collide with a dynamic claim that happened to land
there.

Phase 2 is a simple version of this: every row of every frame, blocking SPI
writes on core 1, no DMA. It proves the panel init and the colours before any
of the optimisation exists.

### 5.2 The keyboard

**What the firmware does.** Read from `Code/picocalc_keyboard` at `fdcfe2c`,
firmware `BIOSVERSION 0x16`:

- I²C slave at 0x1F. A transaction is a write of a register number, with bit 7
  (`WRITE_MASK`) set for a write, then a two-byte read. `receiveEvent()` does
  the work during the **write**. For `REG_ID_FIF` (0x09) that is where the
  FIFO is dequeued, and the read then returns `[state, key]`. An empty FIFO
  returns `[0, 0]`. The FIFO holds 31 events.
- Default config (`reg_init()`): `CFG_OVERFLOW_INT | CFG_KEY_INT |
  CFG_USE_MODS | CFG_REPORT_MODS`, 0xD2.
- **`receiveEvent()` has no case for `REG_ID_CFG`.** A write to 0x02 falls into
  `default` and changes nothing. Shipped firmware therefore always has
  `CFG_USE_MODS` and `CFG_REPORT_MODS` set. The brief's open question about
  this is settled from source. **On the unit (2026-10-30) the version
  register (0x01) reads `00 00`, not 0x16**, but every behaviour below was
  observed exactly as described, so section 7 stands.
- With `CFG_REPORT_MODS`, modifiers arrive as their own events: Alt 0xA1,
  left Shift 0xA2, right Shift 0xA3, Ctrl 0xA5. Caps Lock 0xC1 arrives as an
  ordinary key, and the firmware keeps its own toggle state.
- With `CFG_USE_MODS`, the code depends on the modifiers at the moment of the
  event (`transition_to()`):
  - Shift held and the key is not a letter: report the entry's second
    legend, `symb` ('3'→'#', F1→F6, Esc→Break).
  - Letters come as 'A'–'Z' with Shift or Caps, otherwise 'a'–'z'.
  - Alt held: the code is unchanged except that Alt+I reports Insert, and
    Alt + `,` `.` Space `B` are **swallowed**. The firmware uses them for
    screen backlight, keyboard backlight and battery display.
- State is 1 pressed, 2 hold, 3 released. When a key reaches hold (300 ms),
  printable keys, Enter, Tab, Del, Backspace and arrows re-report as
  *pressed*. Other keys report *hold*.

**Consequences for the driver.**

1. **Press and release can carry different codes.** Press 3, press Shift,
   release 3, and the release arrives as '#'. The layer keeps a table keyed by
   *physical* key: each PicoCalc code maps to its physical key, and that key
   remembers which BBC key, and which forced SHIFT state, it produced on
   press. A release always undoes exactly what its press did.
2. **Repeated presses are ignored.** A press for a physical key already down
   is the hold re-report, and is dropped. The BBC OS does its own
   auto-repeat.
3. **No sleeping, no blocking.** The RP2350 I²C block queues commands in its
   16-entry TX FIFO and starts a new transaction by itself after a STOP. Each
   poll (once per 20 ms frame, from `frank_keyboard_poll()`) first collects
   the read bytes left in the RX FIFO by the previous poll. It then queues up
   three `[write 0x09, STOP][read, read+STOP]` transactions and returns at
   once. **Settled on hardware:** the STM32 needs no gap between the write and
   the read. At ClockworkPi's 10 kHz, one transaction takes about 5 ms, so
   three a frame is about 15 ms of bus time run by the hardware, and up to
   150 events a second against about 10 from a held key.
4. **Boot.** The FIFO was found full of stale events at boot. Events are
   discarded until the first empty read. The version register is not read,
   since it says nothing useful on this firmware.

**Two things found on hardware that change nothing above but must not be
retried or tripped over:**

- **Registers 0x0C and 0x0D**, the "C64" raw matrix and joystick bits, answer
  with their headers but never showed a key down on the unit. Reading physical
  key state that way would have removed every code-mapping problem above. It
  does not work on this firmware.
- **Alt plus left Shift switches on a hidden NumLock** (`next_item_state()` in
  `keyboard.ino`). The firmware then treats every key as Alt-modified and
  swallows Space, `,`, `.` and `B` (its backlight and battery chords) until
  Shift is next pressed with another key. Nothing on the Pico can turn it off.
  This is why SHIFT-BREAK is Alt+Esc rather than Alt+Shift+Esc. Alt plus
  right Shift sets the firmware's caps state instead, which the layer
  ignores.

**Where it lives.** `drivers/picocalc_kbd.c` owns the I²C and yields
`(state, code)` pairs. The PicoCalc-to-BBC layer is
`src/frank/frank_kbd_picocalc.c`, built for `picocalc` only and called from
`frank_keyboard_poll()` behind `PLATFORM_PICOCALC`. It emits
`ALLEGRO_KEY_*` codes directly rather than going through XT scancodes, because
two of the BBC keys it needs (`\|` via `ALLEGRO_KEY_BACKSLASH2`, and SHIFT LOCK
via `ALLEGRO_KEY_PAD_EQUALS`) have no XT code in `xt_to_allegro[]`. The
overlay and chord handling in `process_xt_event()` is about ten lines, and is
repeated in the new file rather than factored out, so that no other board's
build changes.

### 5.3 A console that cannot block core 0

The SDK's `stdio_uart` writes with `uart_putc()`, which spins while the 32-byte
TX FIFO is full. At 115,200 baud that is 87 µs a character, so a 100-character
status line costs core 0 about 6 ms. The USB-CDC builds avoid this with
`PICO_STDIO_USB_STDOUT_TIMEOUT_US=0`. There is no UART equivalent.

So `picocalc` gets its own stdio driver:

- A 2 KB TX ring filled by `out_chars` and drained by the UART TX interrupt.
  When the ring is full, characters are dropped and counted, and the count is
  printed when there is room.
- RX goes through a small ring, filled by the RX interrupt, which serves
  `getchar_timeout_us(0)` for `frank_console.c`.

It is registered with `stdio_set_driver_enabled()`, and
`pico_enable_stdio_uart` stays off for `picocalc`. `drivers/uart_logging.c` is the
nearest existing code, but it writes with blocking `uart_putc_raw`. The Fruit
Jam's UART1 console has the same latent cost. That is recorded as a finding
and not changed here.

---

## 6. Clocks

`clk_sys` stays at 252 MHz (N7). It is not a constraint here, since there is
no PIO video and no HSTX, but the emulator is tuned there and nothing needs
more.

`clk_peri` is on `pll_usb` at 48 MHz after `set_sys_clock_khz()`, which caps
SPI at 24 MHz (SSP divides by at least 2). Since Phase 5, `main()` re-points
it at `clk_sys`, undivided, at 252 MHz, giving SPI at 63, 42, 31.5 or
25.2 MHz. The LCD runs at 63, its reads of the scan line at 8, and the SD
card at 25.2 for its requested 30. That is the same
operation `hdmi_hstx_clock_init()` does for the Fruit Jam, with the same rule:
do it in `main()` immediately after `set_sys_clock_khz()`, before the UART,
SPI or I²C are initialised, because each computes its divisor from
`clock_get_hz(clk_peri)` once. The UART then recomputes 115,200 baud from
252 MHz, which the fractional baud divider handles.

`pll_usb` stays as it is, so the Pico's own USB-CDC remains possible, though
it is not used.

---

## 7. The key map

### 7.1 Agreed (2026-10-30)

- **Symbols type what the PicoCalc key says.** Letters, digits, space, arrows
  and modifiers are positional: a PicoCalc key is a BBC key, and PicoCalc Shift
  is BBC SHIFT. For a symbol, the layer presses the BBC key that produces that
  character and forces BBC SHIFT on or off to suit while it is held. Games that
  read the matrix see the same letter, digit and arrow positions as anywhere
  else.
- **Function keys: F1=f1 … F9=f9, F10=f0.** F1–F5 are plain keys. F6–F10 are
  their Shift layer, so BBC SHIFT is forced off while one is held, and `*KEY`
  strings expand.
- **Chords:** **Alt+D** disc loader (F11 elsewhere), **Alt+S** settings (F12
  elsewhere), **Alt+P** screenshot (Print Screen elsewhere), **Ctrl+Alt+Del**
  hard reset (unchanged). The BBC has no Alt key, so Alt never reaches the
  emulated machine.

### 7.2 Special keys, agreed 2026-10-30

| BBC key | PicoCalc | Note |
|---|---|---|
| BREAK | Shift+Esc | BBC SHIFT forced off, so this is a plain BREAK |
| SHIFT-BREAK | Alt+Esc | Autoboot by hand. The F11 loader already autoboots drive 0. Was Alt+Shift+Esc; changed 2026-10-30 because Alt + left Shift trips a firmware trap (section 5.2) |
| CTRL-BREAK | Ctrl+Shift+Esc | |
| ESCAPE | Esc | |
| COPY | Shift+Del (the End legend) | BBC SHIFT forced off |
| DELETE | Backspace, and Del | Both. The BBC DELETE is a backspace |
| TAB | Tab | Shift+Tab (Home) is SHIFT+TAB |
| RETURN | Enter | Shift+Enter (Insert) is SHIFT+RETURN |
| CAPS LOCK | Caps | Press and release passed through. The firmware's own caps state only changes letter case, which the layer ignores |
| SHIFT LOCK | Alt+Caps | `ALLEGRO_KEY_PAD_EQUALS`, which B-em maps to the SHIFT LOCK position |
| CTRL | Ctrl | |
| SHIFT | either Shift | Counted, so releasing one with the other held keeps BBC SHIFT down |
| Cursor keys | arrows | Shift+↑/↓ (PgUp/PgDn legends) stay SHIFT+cursor |
| f0–f9 | F10, F1–F9 | As agreed |

How BREAK is done: B-em has no BREAK in its key matrix. Its own BREAK key
(the F12 case of `main_key_down()` in `src/bem/main.c`) calls
`m6502_reset()`, `video_reset()` and `wd1770_reset()`, and the layer does the
same, with BBC SHIFT and CTRL set as the table says. The MOS reads SHIFT and
CTRL shortly after the reset, so that state is held for a second however
briefly the key is tapped, as the autoboot in `frank_disc.c` does.

Allegro codes, for anyone reading `frank_kbd_picocalc.c`: B-em's
`ALLEGRO_KEY_F1` is BBC **f0**, so f1–f9 are `ALLEGRO_KEY_F2`–`F10`. COPY is
`ALLEGRO_KEY_END`.

### 7.3 Symbols

The BBC key that produces each character, and the SHIFT state forced while it
is held. `⇧` means SHIFT forced on, `–` means forced off. The Allegro code is
what B-em's `allegro2bbc[]` maps to that BBC position. B-em's PC layout puts
BBC keys on PC positions, which is why the names do not match.

| PicoCalc gives | BBC key | SHIFT | Allegro code |
|---|---|---|---|
| `!` `"` `#` `$` `%` | 1 2 3 4 5 | ⇧ | `KEY_1`…`KEY_5` |
| `&` `'` `(` `)` | 6 7 8 9 | ⇧ | `KEY_6`…`KEY_9` |
| `@` | @ | – | `OPENBRACE` |
| `^` | ^ ~ | – | `EQUALS` |
| `~` | ^ ~ | ⇧ | `EQUALS` |
| `-` | − = | – | `MINUS` |
| `=` | − = | ⇧ | `MINUS` |
| `_` | _ £ | – | `TILDE` |
| `` ` `` | _ £ | ⇧ | `TILDE`. Types code &60, which MODE 7 shows as £ |
| `[` `{` | [ { | – / ⇧ | `CLOSEBRACE` |
| `]` `}` | ] } | – / ⇧ | `BACKSLASH` |
| `\` `\|` | \ \| | – / ⇧ | `BACKSLASH2` |
| `;` `+` | ; + | – / ⇧ | `SEMICOLON` |
| `:` `*` | : * | – / ⇧ | `QUOTE` |
| `,` `<` `.` `>` `/` `?` | same keys | – / ⇧ | `COMMA`, `FULLSTOP`, `SLASH` |

Every PicoCalc symbol has a BBC equivalent, so nothing is dropped. £ is
reached through `` ` ``.

### 7.4 Joystick (F14, Phase 6)

The existing gamepad presets in `frank_settings.c` ("Arrows", "Z X : /")
already map abstract pad actions to BBC keys. A PicoCalc source can feed the
same `frank_gamepad_poll()` merge: arrows as the d-pad, Alt as fire 1, and
Ctrl as fire 2. Alt is free because the BBC has no Alt key. Deferred.

---

## 8. Phased plan

Each phase ends with something to flash and check in a minute, and none starts
until the previous one's exit check has passed on hardware. Each phase is its
own branch, `picocalc-phaseN`, so a failed one can be dropped. The order
follows the Fruit Jam's: never debug a new board and a new video driver at the
same time.

### Phase 0: plan, no code

This document, the `CLAUDE.md` section, the `docs/HANDOFF.md` entries.
**Exit:** you have agreed section 7 and this plan. **Passed 2026-10-30.**

### Phase 1: boots blind

- `src/board_picocalc.h`: the pins in section 2.1, `HAS_PWM` only, no PSRAM pin.
  `board_config.h` gets a `PLATFORM_PICOCALC` branch.
- CMake: `picocalc` in the platform list, with `PICO_BOARD=pico2` and
  `rp2350-arm-s`. A new `HDMI_DRIVER=LCD_SPI`, forced for `picocalc` and rejected
  elsewhere, mirroring the `HSTX` gating. `USB_HID=1` rejected for `picocalc`. The
  SD pins passed as compile definitions. `build.sh` documents `picocalc`.
- `drivers/lcd_st7365p.c` as a stub with the `graphics_*` interface and no
  panel I/O.
- `drivers/uart_stdio_nb.c` (section 5.3).
- `main()`: no LED, no PSRAM probe on `picocalc`. The PSRAM pin default is not
  enough, because it is GPIO 0.
- A once-a-second line from `frank_perf_tick()`: frame count, emulation
  speed as a percentage of 50 Hz, and a checksum of BBC RAM &3000–&7FFF.

**Signed off 2026-10-30, after running on a Pico 2 W; branch `picocalc-phase1`.** The
console, the SD mount, PWM audio and the disc autoboot all work. Speed is 100%
in bitmap modes and 92 to 94% in MODE 7, CPU-bound in the teletext rasteriser
and the row conversion; Phase 2's LCD blit is where to win that back. See
`docs/HANDOFF.md`.

**Exit:** the console shows 100% speed and the SD card mounts. The checksum
changes when a disc autoboots. For this phase the disc is
`/micro/disk/drivea.ssd`, which `frank_disk_autoload()` mounts with no
`micro.ini`, because there is no keyboard to use the loader.

### Phase 2: first picture

Panel init in RGB565 and a whole-frame push on core 1, blocking SPI, at 24 MHz.
The console reports display frames per second.

**Exit:** the MOS banner and a BASIC prompt readable in MODE 7 (the default
boot screen) and in one bitmap mode, with correct colours. Since there is no
keyboard yet, a disc whose `!BOOT` runs `MODE 1` gives the bitmap mode.

Also carried in from Phase 1: MODE 7 at 100%.

**Signed off 2026-10-30, after running on a Pico 2 W; branch `picocalc-phase2`.** The panel is driven
and the console shows 14–15 frames a second sent to it. Every emulated frame
now runs at 100%, MODE 7 included, at 69–74% busy.

**Looked at on the panel, 2026-10-30:** the picture is centred with black
bands above and below, the right way up, not mirrored and not inverted. A
temporary build drew pure red, green and blue bars (RGB565 `F800`, `07E0`,
`001F`) in the top band, and they showed as red, green and blue, so the
colour order is right as built. The demo disc covers MODE 7 and bitmap
modes. The MOS banner and BASIC prompt were then checked by booting from a blank
disc (the swap in `docs/HANDOFF.md`), and were readable in MODE 7.

**All Phase 2 exit criteria met on hardware, 2026-10-30.**

### Phase 3: keyboard

`drivers/picocalc_kbd.c` and the section 7 layer. Every event goes to the
console as `KBD st=N key=0xNN -> al=N [shift forced on|off]` until Phase 6
quietens it. `al` is the Allegro code sent to B-em, since the BBC matrix
table is private to `keyboard.c`.

**Exit:** typing and running a BASIC program works. Every key in section 7
works. Alt+D and Alt+S open the loader and settings.

**Signed off 2026-10-31, after running on a Pico 2 W; branch `picocalc-phase3`.**
The user typed and ran BASIC with every symbol, and checked:

- COPY, DELETE and Caps
- f-keys with `*KEY`
- BREAK and CTRL-BREAK
- Alt+D and Alt+S

All keys are correct, with no I²C errors. Keyboard polling does not show in
`busy`. Two fixes came out of testing:

- **Overlay flicker on the LCD,** fixed by the HSTX build's row-span and
  redraw-on-change mechanisms.
- **A hang at 17.9 minutes,** which affects every board. It is fixed here
  with `-fwrapv`, and the build has now run past 20 minutes. See
  `docs/HANDOFF.md`.

RAM is 314,548 B. The frozen boards are byte-identical once the build date
is normalised.

### Phase 4: sound

`frank_audio.c` and `frank_settings.c` gated to PWM only on `picocalc`.
`pwm_audio_push_samples_blocking()` replaced on this path with a non-blocking
push that drops when the ring is full and counts it (N3). The drop count goes
on the status line.

**Exit:** a game with music plays for five minutes with no audible dropouts and
a drop count of zero.

**Signed off 2026-11-01, after running on a Pico 2 W; branch `picocalc-phase4`.** The
settings and backend were already PWM-only from Phase 1, so this phase is
the driver: `drivers/pwm_ring_audio.c`, described in section 3. `STAT`
gains four fields:

- `snd`: samples queued after the last push. It hovers near 2,048.
- `trim`: samples skipped (+) or repeated (−), cumulative.
- `und`: underruns.
- `sdrop`: blocks dropped for lack of room. Expected only with Limit Speed
  off.

Measured over twenty minutes, past the 17.9-minute mark: `busy` was flat
at 76–77%, `und` and `sdrop` were 0, and
the producer runs 5 ppm fast. `trim` moves about 50 in the first minute,
while the fill settles from where the first push placed it, and is then
still. RAM is 317,632 B.

You checked by ear:

- **Music in a game for five minutes:** no gaps, clicks or drone.
- **Volume:** works.
- **`SOUND` from BASIC:** works.
- **The disc loader:** sound carries on while it is open. It does not stop
  the emulator.
- **A screenshot:** stops emulator and sound for a fraction of a second,
  then both carry on.

Two fixes came out of that testing:

- **Switching sound off looped the last 131 ms** until sound came back on.
  With sound off the emulator produces nothing at all, so the DMA kept
  replaying the ring. Each push silences the part of the ring the output
  has already played. So does `pwm_ring_service()`, once a frame, because
  with sound off there are no pushes. That second part was missing from the
  first fix, and the loop came back.
- **MODE 7 lost text on coloured backgrounds:** the test disc showed it. The
  teletext renderer picked the background by brightness, and every primary
  colour ties at full brightness. The PicoCalc renderer now snaps each sample
  on its own (`docs/HANDOFF.md`).

### Phase 5: speed

Changed-row tracking, double line buffers on DMA, `clk_peri` on `clk_sys`, then
SPI stepped 25.2 → 31.5 → 42 → 63 MHz while the picture stays clean. The rate
per mode is recorded.

**Tearing, seen by the user at the Phase 4 sign-off (2026-11-01).** The
Twisted demo's animations tear, most clearly its moving checkerboards. The
cause is expected: core 1 sends about 15 frames a second while the emulator
draws 50, and with `TE` unwired nothing ties a send to the panel's scan. The
Phase 5 plan has to say what it does about this, and Twisted is the test.

**Exit:** N2 met, measured rates in `docs/HANDOFF.md`.

**Signed off 2026-11-02, after running on a Pico 2 W; branch `picocalc-phase5`.**
Five steps, each run on the board:

1. `clk_peri` on `clk_sys` (section 6).
2. Whole frames only (section 5.1, item 5). The panel stopped getting rows
   from several BBC frames in one picture.
3. DMA and changed rows. `STAT` gains `rows`, the rows sent a second.
4. The SPI clock stepped 31.5, 42 and 63 MHz, live from the debugger. The
   user saw no fault at any of them, and no difference between them; 63
   is built in.
5. Writes timed to the panel's scan (section 5.1, item 7). `STAT` gains
   `lwait`, milliseconds a second spent waiting for it. The user compared
   it on and off on Twisted, with a marker on screen to say which: "Less
   tearing on green (very good)".

**Rates.** A 25-minute run of the demo disc, emulator at 100% throughout,
no underruns. What sets the rate is how much of the screen changes, not
the mode:

| Rows changed per frame | Panel frames per second |
|---|---|
| under 10: text, a cursor, a still picture | 50 |
| 10–50 | 51 on average, never below 44 |
| 50–150 | 41 on average, never below 34 |
| 150 or more: whole-screen motion, scrolling | 26 on average, never below 24 |

A whole changed frame takes about 23 ms at 63 MHz, just over one emulated
frame, so it lands on every other one. RAM 322,408 bytes (61.5%).

`und` rises by one for each screenshot, and probably for settings saves:
both stop core 0 for an SD write long enough to empty the audio ring. The
user accepts this, since a menu or screenshot is already interrupting play
(`docs/HANDOFF.md`).

### Phase 6: polish

Status line in the spare rows (F11), battery and keyboard backlight (F12), the
joystick source (F14), optionally a MODE 0/3 full-width option (F13), a release
build, and the README section.

**Exit:** the brief's end-to-end checklist passes.

---

## 9. Resource budget

**RAM.** `picocalc` drops every video driver's buffers and keeps the two 160 KB
framebuffers. The closest existing build is `HDMI_PIO` on m2, at 297,268 bytes
(56.7%), and it carries the PIO HDMI line buffers and tables. The LCD driver
adds about 3.3 KB: a 512-byte RGB565 table, two 640-byte line buffers, 1 KB of
row sums, and the UART rings. Measured: 277,792 bytes in Phase 1, and
313,212 (59.7%) in Phase 2, which adds the 32 KB RGB555 lookup table that
makes the blit cheap enough for MODE 7 (see `docs/HANDOFF.md`). Phase 5,
with its row sums, line buffers and second hash array, is 322,408 (61.5%).
Well inside
N5's ceiling, the `COMPOSITE` build at 393,000 bytes (75%). Core 1's LCD loop runs from RAM
(`__not_in_flash_func`), per `CLAUDE.md` constraint 4, though it is no longer
real-time.

**Flash.** 4 MB on the Pico 2 and 2 W. Every existing build is under 9%.

**PIO.** None used. All three blocks are free.

**DMA.** PWM hardcodes 8 and 9; on `picocalc` they are the ring and its
loop channel. The LCD claims two dynamically for its TX and RX, which
land on 0 and 1, so the order against audio init does not matter. It also
claims one hardware spin lock, for the frame handover. The I2S code is compiled but never initialised on `picocalc`, so 10
and 11 are idle.

**IRQs.** `DMA_IRQ_2` PWM, `UART0_IRQ` console, and one `DMA_IRQ_n` for the
LCD if Phase 5 needs a completion interrupt. Core 1 can poll instead.

**Not used, and must stay that way:**

| GPIO | Why |
|---|---|
| 0 as anything but UART0 TX | The PSRAM probe default (section 1) |
| 2–5, 20, 21 | PSRAM. Unused but wired |
| 8, 9 | UART1 to the STM32's UART3. Note that m1 and m2 use 8 as their PSRAM CS, so copying their header would drive it |
| 23, 24, 25, 29 | Pico 2 W internals (section 2.2) |

**The keyboard MCU is also the power manager.** Writing `REG_ID_RST` (0x08)
resets the STM32 after a delay. Writing `REG_ID_OFF` (0x0E) changes its
power-off behaviour. The STM32 also gates the Pico's own power through
`PICO_EN`. The driver writes neither register and only reads 0x01, 0x09 and,
in Phase 6, 0x0B, 0x05 and 0x0A.

---

## 10. Open questions

| Question | Blocks | How to settle |
|---|---|---|
| What SPI clock does the panel take before the picture corrupts? | N2 | Phase 5, stepping up from 24 MHz |
| Does the STM32 need a gap between the FIFO write and the read? | Keyboard throughput | **Answered 2026-10-30: no.** Back-to-back transactions work |
| Does the unit's keyboard firmware report version 0x16? | Whether section 5.2 applies | **Answered 2026-10-30: it reads `00 00`**, but behaves exactly as 0x16's source describes, so section 5.2 applies |
| Is the `TE` pin really unconnected, rather than lost in text extraction? | Tear-free display | A visual check of the schematic's LCD connector |
| Does the CYW43 stay off with `WL_ON` undriven? | Nothing functional; power draw | Current draw, W against non-W |
| Is a plain BREAK `m6502_reset()` or `main_reset()`? | Correct BREAK | **Answered 2026-10-30: neither alone.** B-em's BREAK is `m6502_reset()`, `video_reset()` and `wd1770_reset()`; section 7.2 |
| Does the panel need `INVON`, and does `MADCTL` 0x48 give the right orientation? | Phase 2 colours | **Answered 2026-10-30: yes, and yes.** With `INVON`, `MADCTL` 0x48 and `LCD_SWAP_RB` 0, the picture is upright, unmirrored and not inverted, and pure red, green and blue bars show true |
| Is a Phase 6 battery reading percent alone, or has it a charging flag? | Status line wording | Read `battery.ino` in Phase 6 |

---

## 11. Testing

The compile sweep in `CLAUDE.md` section 9 gains `picocalc`:

```bash
for p in m2 m1 z0 fj picocalc; do for d in HDMI_PIO_AUDIO HDMI_PIO COMPOSITE LCD_SPI; do
  [ "$p" = picocalc ] && [ "$d" != LCD_SPI ] && continue
  [ "$p" != picocalc ] && [ "$d" = LCD_SPI ] && continue
  [ "$p" = z0 ] && [ "$d" = COMPOSITE ] && continue
  [ "$p" = fj ] && [ "$d" = COMPOSITE ] && continue
  for u in 0 1; do
    [ "$p" = picocalc ] && [ "$u" = 1 ] && continue
    PLATFORM=$p HDMI_DRIVER=$d USB_HID=$u ./build.sh \
      || echo "FAILED: $p $d hid=$u"
  done
done; done
```

A `picocalc` ELF must contain the console driver:
`arm-none-eabi-nm build/frank-micro.elf | grep uart_stdio_nb` must not be
empty. New drivers are checked with
`grep -n 'sleep_\|busy_wait\|_blocking' drivers/lcd_st7365p.c
drivers/picocalc_kbd.c drivers/uart_stdio_nb.c drivers/pwm_ring_audio.c`. Anything left must be on core
1 or justified next to it.

On hardware, the brief's checklist:

- [ ] Cold boot to the Acorn MOS screen
- [ ] A MODE 7 screen and a bitmap mode. For MODE 7, `tools/mkmode7test.py`
  builds a test disc covering colours, backgrounds, double height and
  graphics. Mount it with Alt+D
- [ ] Mount a disc with Alt+D and SHIFT-BREAK autoboot it
- [ ] Every key in section 7, including BREAK, ESCAPE, COPY and f0–f9
- [ ] Sound through the speakers with no dropouts over five minutes
- [ ] Boot with no SD card. The error screen should stay up;
  `docs/INVESTIGATION.md` finding 4 says it currently reboots
- [ ] Thirty minutes of play with no watchdog reset or lockup
- [ ] Display frame rate in MODE 7, MODE 1 and a scrolling game, recorded in
  `docs/HANDOFF.md`

Every result records the module: **Pico 2 W** for now. The non-W is untested
until someone runs it.

Before every flash, hold BOOTSEL while connecting the Pico's USB, then copy
`build/frank-micro.uf2` to the drive (`CLAUDE.md` section 8).

**This machine.** Neither the Pico SDK nor `arm-none-eabi-gcc` was found on the
machine where Phase 0 was written (2026-10-30). `tools/setup.sh` installs them
and is the first step of Phase 1.

---

## 12. What this document's claims rest on

### Provenance

| Thing | Identifier |
|---|---|
| frank-micro | `fruit-jam-port` commit `d8d25f5` |
| rh1tech `main` | still `c27ffbf` on 2026-10-30. The Fruit Jam work is not upstream, so the brief's fallback did not apply |
| `clockworkpi/PicoCalc` | `master` at `fdcfe2c`, fetched 2026-10-30, not cloned |
| Mainboard schematic | `clockwork_Mainboard_V2.0_Schematic.pdf` from that commit, text-extracted with `pdftotext -raw` |
| ST7365P datasheet | `ST7365P_SPEC_V1.0.pdf` from that commit |
| Pico 2 W datasheet | `datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf`, fetched 2026-10-30 |

### Claims by strength of evidence

| Class | Claims |
|---|---|
| Read, primary | Every GPIO in section 2.1, from the schematic's Pico-socket net labels. The UART1–STM32 link. The PSRAM quad lines. The CH340C and WAS7227 part numbers. Keyboard: address, `WRITE_MASK`, the FIFO-on-write behaviour, the missing CFG write case, the default config, the key tables, the mod handling, the Alt swallowing, `BIOSVERSION 0x16`. ST7365P: `TSCYCW` 66 ns, the 65K SPI format. Pico 2 W GPIO roles. Every frank-micro symbol cited |
| Read, but from text extraction | `TE` has no net. That is the absence of a label in `pdftotext` output, so it is weaker than a positive read. Confirm visually |
| Derived | Bandwidth and frame-rate table (section 5.1). SPI divisor steps. RAM estimate. That `WL_ON` stays low. That `SW701` selects the USB-C serial path |
| Assumed | That the unit runs firmware 0x16. That the I²C controller's queued transactions suit the STM32's Wire slave with no gap. (`pico2.h`'s default UART on GPIO 0/1 and LED on 25 was assumed here, and read from SDK 2.2.0 on 2026-10-30) |

### Sources

- `clockworkpi/PicoCalc` at `fdcfe2c`: `Code/picocalc_keyboard/`
  (`keyboard.h`, `keyboard.ino`, `reg.h`, `reg.ino`, `fifo.ino`,
  `conf_app.h`, `picocalc_keyboard.ino`), `Code/picocalc_helloworld/lcdspi/`,
  the mainboard v2.0 schematic, the ST7365P datasheet
- Raspberry Pi, *Pico 2 W datasheet*
- `REQUIREMENTS.md`, 2026-10-07
