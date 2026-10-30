# frank-micro on PicoCalc: requirements and overview

Oct 7, 2026 · @Tim

## Purpose

Port the frank-micro BBC Micro emulator (B-em core) to the ClockworkPi PicoCalc fitted with a Raspberry Pi Pico 2 (RP2350), as a new board `pc` alongside the existing ones.

This is the brief for Claude Code. Read it in full before the first change. It says what to build, what to read in the repository first, what the hardware is believed to be, how to phase the work, and what counts as done.

Two rules frame everything below:

- **The emulator core is not the job.** B-em and Graham Sanderson's Pico work already run at full speed on the RP2350. The job is a new platform layer: an LCD scanout, a keyboard driver, and a board header.
- **Claude Code cannot see the screen or press the keys.** Every phase must end in something the human can flash and check in a minute, and the firmware must report its own state over serial so most debugging can happen from text.

## Starting point

Fork [adq/frank-micro](https://github.com/adq/frank-micro), branch `fruit-jam-port`. It is rh1tech's `main` plus 14 commits that add a fourth board, gate PS/2 and the NES pad behind capability macros, and add four documents that map the codebase.

| Candidate | Why not, or why |
| --- | --- |
| [kilograham/b-em](https://github.com/kilograham/b-em) | The origin of the Pico port. Last touched August 2024 (`pico2` branch); no board abstraction. Reference only. |
| [rh1tech/frank-micro](https://github.com/rh1tech/frank-micro) `main` | Good base, but assumes PS/2 and HDMI on every board. |
| [adq/frank-micro](https://github.com/adq/frank-micro) `fruit-jam-port` | **Use this.** The gating work is done, and the docs save days. |

If the Fruit Jam work has been merged into rh1tech's `main` by the time you start, base on `main` instead and say so in `docs/HANDOFF.md`.

### Read these first, in this order

1. `CLAUDE.md`: what is live and what is dead. Only about 13,000 of the repository's 180,000 lines are compiled. 32 of the 34 files in `src/Pico/` are dead and will not compile.
2. `docs/HANDOFF.md`: decisions, dead ends, and the rule for adding entries.
3. `docs/FRUIT-JAM.md`: the model for this port. Copy its structure for `docs/PICOCALC.md`.
4. `docs/INVESTIGATION.md`: only the findings you need. It is evidence, not instructions.

Do not follow `PERFORMANCE.md`. It describes the abandoned beebjit architecture.

## Target hardware

The target is a PicoCalc mainboard v2.0 with a Raspberry Pi Pico 2 (RP2350A, 520 KB SRAM, 4 MB flash). An RP2040 Pico is out of scope: frank-micro needs RP2350 SRAM and the Thumb 6502 core targets it.

Every pin below was read from ClockworkPi's own sample code and schematic, not from frank-micro. Confirm each against the [mainboard schematic](https://github.com/clockworkpi/PicoCalc/blob/master/clockwork_Mainboard_V2.0_Schematic.pdf) before relying on it, and record the confirmation in `docs/PICOCALC.md`.

| Function | Peripheral | GPIO | Read from |
| --- | --- | --- | --- |
| LCD SCK, MOSI, MISO | `spi1` | 10, 11, 12 | [`lcdspi.h`](https://github.com/clockworkpi/PicoCalc/blob/master/Code/picocalc_helloworld/lcdspi/lcdspi.h) |
| LCD CS, DC, RST | GPIO | 13, 14, 15 | same |
| SD SCK, MOSI, MISO, CS | `spi0` | 18, 19, 16, 17 | [`sd_boot/config.h`](https://github.com/clockworkpi/PicoCalc/blob/master/Code/pico_multi_booter/sd_boot/config.h) |
| SD card detect, active low | GPIO, pull-up | 22 | same |
| Keyboard SDA, SCL | `i2c1`, address `0x1F` | 6, 7 | [`i2ckbd.h`](https://github.com/clockworkpi/PicoCalc/blob/master/Code/picocalc_helloworld/i2ckbd/i2ckbd.h) |
| Audio left, right | PWM slice 5 A and B | 26, 27 | [`pwm_sound.h`](https://github.com/clockworkpi/PicoCalc/blob/master/Code/picocalc_helloworld/pwm_sound) |
| Console | `uart0` | 0 TX, 1 RX | schematic net names `UART0_TX`, `UART0_RX` |
| Onboard PSRAM, 8 MB (not used) | PIO SPI | CS 20, SCK 21, MOSI 2, MISO 3 | helloworld `CMakeLists.txt` |

### The display

The panel is 320×320 and driven by a Sitronix ST7365P; ClockworkPi's code drives it with ILI9488 command names. The [ST7365P datasheet](https://github.com/clockworkpi/PicoCalc/blob/master/ST7365P_SPEC_V1.0.pdf) is in the repository.

Three facts shape the scanout driver:

- **RGB565 works over 4-line SPI.** Section 8.4.2.3 of the datasheet lists 65K colour, `3Ah = 55h`, for the 4-line interface. ClockworkPi's sample uses 18-bit (`0x66`), which costs 50% more bandwidth. Use 16-bit.
- **The rated SPI write clock is slow.** The datasheet gives a 66 ns minimum write cycle, about 15 MHz. The sample runs at 25 MHz. Faster is common practice but is out of spec, so the clock is a tunable, not a constant.
- **A full frame at 50 Hz needs about 66 Mbit/s.** 320×256 pixels × 2 bytes × 50 = 8.2 MB/s. That is beyond the rated clock, so the driver must send only changed rows (see Architecture).

### The keyboard

An STM32 on the keyboard board scans the matrix and exposes a key-event FIFO over I²C ([firmware source](https://github.com/clockworkpi/PicoCalc/tree/master/Code/picocalc_keyboard)). Reading register `0x09` returns one event: a key code and a state, pressed (1), hold (2) or released (3).

- Events carry key-up, so mapping onto the BBC key matrix is possible.
- Modifier keys are reported only when `CFG_REPORT_MODS` is set. With `CFG_USE_MODS` set, Shift changes the reported character. The BBC needs raw key positions, so check and set the config register (`0x02`) at boot.
- There are F1 to F10, Esc, Break, Ctrl, Alt, both Shifts, Caps Lock, arrows, Ins, Del, Home, End, PgUp and PgDn. There is no F11 or F12.
- The sample driver sleeps 16 ms inside every read. That cannot run on the emulation core.

### Clocks

`set_sys_clock_khz()` leaves `clk_peri` on `pll_usb` at 48 MHz, which caps SPI at 24 MHz. adq hit the same trap and documented it in `CLAUDE.md` section 6. Re-point `clk_peri` at `clk_sys` before any SPI is initialised if the LCD needs more than 24 MHz. The SD card shares `clk_peri`.

## Functional requirements

A new platform `pc`, built with `PLATFORM=pc ./build.sh`, boots a BBC Model B to the Acorn MOS prompt on the PicoCalc's own screen, keyboard and speakers, and runs disc images from its SD card.

| ID | Priority | Requirement |
| --- | --- | --- |
| F1 | Must | `PLATFORM=pc` builds a UF2 for the Pico 2, alongside `m1`, `m2`, `z0` and `fj`. |
| F2 | Must | Video on the built-in LCD: the 320×256 frame centred on the 320×320 panel, 32 rows above and below. |
| F3 | Must | All eight BBC display modes render correctly, MODE 7 included. MODE 0 and 3 may stay point-sampled to 320 wide, as on the other PIO builds. |
| F4 | Must | The PicoCalc keyboard drives the BBC key matrix, with key-up handled, so held keys and games work. |
| F5 | Must | Every BBC key is reachable, including ESCAPE, BREAK, COPY, CTRL, SHIFT LOCK, CAPS LOCK and f0 to f9. |
| F6 | Must | The F11 disc loader and F12 settings menu are reachable from a key chord, since the PicoCalc has no F11 or F12. |
| F7 | Must | Disc images load from SD under `/micro/disk/`, with the existing browser, mounting and SHIFT-BREAK autoboot. |
| F8 | Must | SN76489 sound plays on the PicoCalc speakers through the existing PWM backend on GPIO 26 and 27. |
| F9 | Must | A serial console on `uart0` reports boot progress, frame timing and keyboard events. |
| F10 | Should | Model B and Master 128 both work, switchable from F12 as today. |
| F11 | Should | The 64 spare rows show a status line: mounted disc, emulation speed, battery. |
| F12 | Should | Keyboard backlight and battery level are read over the same I²C link. |
| F13 | Could | An option to show MODE 0 and 3 at full 640-pixel width by scaling or panning. |
| F14 | Could | The PicoCalc's arrow keys and the Alt key map to a joystick preset, like the existing gamepad presets. |

The key chords for F6 and the BBC-to-PicoCalc key map are design decisions. Propose them in `docs/PICOCALC.md` and get the human's agreement before building them.

### Out of scope

- Disc write support, the 8271 controller, tubes, tape and save states. They are off in every frank-micro build today.
- RP2040 Pico modules, and the Pico 2 W's wireless.
- Changing behaviour on the four existing boards.

## Constraints and non-functional requirements

Emulation must stay at real BBC speed, 50 frames a second, with the display allowed to lag behind when the SPI link cannot keep up.

| ID | Requirement |
| --- | --- |
| N1 | Emulation runs at 100% of real speed. The display may drop frames; the 6502 and sound may not. |
| N2 | Typical screens (BASIC, MODE 7 text, most games) update at 25 fps or better. Report the measured rate per mode. |
| N3 | No audio dropouts. `give_audio_buffer()` must never block the producer, per `CLAUDE.md` section 6. |
| N4 | Nothing on core 0 blocks on I²C or SPI. The keyboard is polled without sleeping; the LCD is fed by DMA. |
| N5 | Static RAM stays at or below the `COMPOSITE` build, 75% of 512 KB, the tightest existing build. |
| N6 | Runs on a stock Pico 2 with no PSRAM. The PicoCalc's PIO-attached PSRAM is not used. |
| N7 | `clk_sys` stays at 252 MHz unless a measured need is written up in `docs/HANDOFF.md` first. |

### Rules for the codebase

These come from adq's `CLAUDE.md`. Follow them.

- **New work goes in `src/frank/` and `drivers/`, never `src/bem/`.** Local changes to vendored B-em are about 104 lines; keep it that way.
- **Add the board by omitting capabilities.** `board_pc.h` leaves out `HAS_PS2` and `HAS_NESPAD` rather than inventing pins.
- **The other four boards are frozen.** Prefer additive, platform-gated changes. Where shared code must change, write next to it why the other boards are unaffected, and check every build variant still compiles.
- **No work in the per-scanline hook.** `x_gui_end_scanline()` is already overloaded.
- **`printf` must never block core 0.** Keep `PICO_STDIO_USB_STDOUT_TIMEOUT_US=0` and its UART equivalent.
- **Licence.** New files carry the frank-micro GPL-3.0-or-later header. Vendor only GPL-2.0-or-later or permissive code.
- **Commit messages.** The repository's house rule bans AI attribution trailers. Ask the human whether to follow it before the first commit.

## Architecture

Everything up to the 320×256 framebuffer stays as it is; the port replaces what reads the framebuffer (HDMI scanout becomes an SPI LCD push) and what writes the key matrix (PS/2 becomes I²C).

&#91;embedded content: data flow on the PicoCalc · 2 new parts\]

Input enters at the top and leaves at the bottom; the two highlighted boxes are the only new code.

| Part | Change | Where |
| --- | --- | --- |
| B-em core, Thumb 6502, rasteriser | None | `src/bem/` |
| Scanline to framebuffer, 640 to 320 | None | `src/frank/frank_gui.c`, `blit_row()` |
| Double-buffered 320×256 8bpp framebuffer, 160 KB | None | `frank_gui.c` |
| Video scanout | **New**: SPI LCD driver on core 1 | `drivers/lcd_st7365p.c` (suggested name) |
| Keyboard | **New**: I²C FIFO reader and PicoCalc-to-BBC map | `drivers/picocalc_kbd.c`, `src/frank/frank_keyboard.c` |
| Board pins and capabilities | **New** | `src/board_pc.h`, `board_config.h`, `CMakeLists.txt`, `build.sh` |
| SD card | Pins only | `board_pc.h` |
| Audio | Pins only; PWM is the sole backend on `pc` | `board_pc.h`, `frank_audio.c` gating |
| Console | UART0 instead of USB CDC | `board_pc.h`, CMake |
| PSRAM probe | Disabled on `pc` | `board_pc.h` |

### The LCD scanout

The scanout implements the `graphics_*` interface in `drivers/HDMI.h`, as each HDMI and composite driver does today, so `frank_gui.c` does not need to know which one is linked.

1. Core 1 waits for the buffer flip at the start of each emulated frame. The completed buffer is `!current_buffer`; `CLAUDE.md` warns not to "fix" that apparent inversion.
2. It compares each of the 256 rows with what it last sent. A per-row checksum held in RAM is enough; a full shadow copy would cost another 80 KB.
3. For each changed row it converts 320 palette indices to RGB565 through a 256-entry lookup table into one of two 640-byte line buffers, and starts a DMA transfer to `spi1` while it fills the other.
4. A palette change invalidates every row's checksum, so the whole screen is resent.
5. If the next flip arrives before the push finishes, core 1 finishes the rows already started and skips to the newest frame. The emulator never waits for the LCD.

Why this design: a BBC screen is mostly static text, so typical frames send a few rows, and the worst case (full-screen scrolling) degrades to a lower frame rate rather than slowing emulation. The panel exposes no tearing signal that this brief could confirm, so expect some tearing; see Open questions.

The LCD driver must claim its DMA channels dynamically. PWM audio hardcodes channels 8 and 9.

### The keyboard

The keyboard driver runs from `frank_keyboard_poll()`, once per 20 ms frame, and never sleeps.

- Each poll drains the FIFO at `0x09`, up to a small limit, at 400 kHz. If the STM32 needs a gap between the register write and the read, split the transaction across two polls rather than sleeping.
- Each pressed or released event becomes one key\_down() or key\_up() call into B-em, after translating the PicoCalc key code to an ALLEGRO\_KEY code. That is what the PS/2 path in frank\_keyboard.c already does with scancodes. Hold events are ignored.
- With modifiers reported raw, Shift is just another key: PicoCalc Shift maps to BBC SHIFT, and the BBC's own keyboard logic produces shifted characters. The catch is that symbols then follow the BBC layout, not the legends on the PicoCalc keys; see Open questions.

## Phased plan

Seven phases, 0 to 6, each ending in a firmware the human flashes and checks; do not start a phase until the previous one's exit check has passed on hardware.

The order follows adq's reasoning for the Fruit Jam: a new board and a new video driver can each give a black screen, so never debug both at once. Phase 1 proves the board with no display at all.

1. **Phase 0: plan, no code.** Read the four documents. Write `docs/PICOCALC.md` in the structure of `docs/FRUIT-JAM.md`: pin map with sources, what needs no new code, what is new, the key map and menu chords, open questions. Add a PicoCalc section to `CLAUDE.md`, and a dated entry to `docs/HANDOFF.md`.
   - Exit: the human has read and agreed the key map, the menu chords and the plan.
2. **Phase 1: boots blind.** Add `board_pc.h` and the `pc` platform. Stub video and keyboard. UART0 console. SD mounted. The emulator runs and prints, once a second: frame count, emulation speed, and a checksum of BBC screen memory.
   - Exit: the console shows 100% speed, the SD card mounts, and the screen-memory checksum changes when a disc autoboots.
3. **Phase 2: first picture.** LCD init in RGB565, then a whole-screen push of every frame at whatever rate the SPI manages. No row tracking yet.
   - Exit: the MOS banner and a BASIC prompt are readable in MODE 7 and in one bitmap mode. Console reports the display frame rate.
4. **Phase 3: keyboard.** Non-blocking FIFO reader, the agreed key map, the F11 and F12 chords.
   - Exit: typing a BASIC program and running it works; every BBC key on the agreed map works; the disc loader and settings menu open.
5. **Phase 4: sound.** PWM backend on GPIO 26 and 27; remove the HDMI and I2S choices from the `pc` settings menu.
   - Exit: a game with music plays for five minutes with no audible dropouts.
6. **Phase 5: speed.** Changed-row tracking, double line buffers on DMA, `clk_peri` retargeting, and the SPI clock raised step by step while the picture stays clean.
   - Exit: N2 is met and the measured rates per mode are recorded in `docs/HANDOFF.md`.
7. **Phase 6: polish.** The Should and Could requirements, a release build, README section.
   - Exit: the hardware checklist below passes end to end.

## Verification

There is no CI and no test suite, so every change is checked twice: a build of every variant by Claude Code, then a hardware check by the human.

### What Claude Code checks

- **Every variant builds.** Extend the sweep loop in `CLAUDE.md` section 9 with `pc` and run it after every change to shared code. A failure on `m1`, `m2`, `z0` or `fj` means the change broke a frozen board.
- **The `pc` ELF has a console.** `arm-none-eabi-nm build/frank-micro.elf | grep stdio_uart` must not be empty.
- **RAM stays in budget.** Report the static RAM figure for `pc` after each phase against N5.
- **No blocking calls on core 0.** Grep the new drivers for `sleep_`, `busy_wait` and blocking SPI or I²C calls, and justify any that remain.

### What the human checks

Claude Code cannot see the screen. For each phase it states exactly what to flash, what to look for and what to paste back from the console. Before every flash, the human holds BOOTSEL while connecting USB; `CLAUDE.md` section 8 explains that bad firmware can lock the RP2350 in a state only BOOTSEL recovers from.

The end-to-end checklist, adapted from `CLAUDE.md` section 9:

- [ ] Cold boot to the Acorn MOS screen
- [ ] A MODE 7 screen and a bitmap mode, which take different blit paths
- [ ] Mount a disc from the loader chord and SHIFT-BREAK autoboot it
- [ ] Every key on the agreed map, including BREAK, ESCAPE, COPY and f0 to f9
- [ ] Sound through the speakers with no dropouts over five minutes
- [ ] Boot with no SD card, and confirm the error screen stays up (adq's finding 4 says it currently reboots)
- [ ] Thirty minutes of play with no watchdog reset or lockup
- [ ] Measured display frame rate in MODE 7, MODE 1 and a scrolling game, recorded in `docs/HANDOFF.md`

Report only what was actually run. A change is never described as verified on hardware that nobody tested it on.

## Working rules for Claude Code

Keep the repository's documents current as the work goes, so any later session can pick up without re-deriving anything.

| Document | What to add |
| --- | --- |
| `docs/PICOCALC.md` | New. The plan, in the shape of `docs/FRUIT-JAM.md`, with a provenance section rating each hardware claim as read from a file, derived, or assumed. |
| `CLAUDE.md` | A PicoCalc section beside the Fruit Jam one: build command, console, capability macros, the scanout design, the hazards found. |
| `docs/HANDOFF.md` | A dated entry for each decision, dead end and hardware result. Append only; never rewrite an old entry. |
| `README.md` | Phase 6 only: the `pc` platform, pins, and the key map. |

How to work:

- **Plan before code.** Use plan mode for phase 0 and at the start of each later phase.
- **Ask before guessing on anything the human can see.** Key legends, which chord feels natural, whether tearing is acceptable: these are the human's calls.
- **Make the firmware describe itself.** Every phase adds console output that lets a pasted log replace looking at the screen.
- **One phase per branch or pull request**, so a phase that fails on hardware can be dropped without losing the others.
- **Cite by symbol, not only by line.** adq's documents warn that line numbers drift; name the function as well.

## Open questions and risks

The display rate is the main risk; everything else is known work.

| Question or risk | How to settle it |
| --- | --- |
| How fast does the panel accept SPI writes before the picture corrupts? | Phase 5: step the clock up from 25 MHz and record the last clean value. |
| Does the panel's tearing-effect output reach a GPIO? | Read the schematic in phase 0. If not, accept tearing or pace pushes to the emulator's own frame. |
| Does the keyboard firmware need a gap between the register write and the read? | Phase 3: try back-to-back first, then split across polls. |
| Can the keyboard config register turn off `CFG_USE_MODS` on the shipped firmware? | Phase 3: read the firmware version (`0x01`) and test. |
| BBC symbols follow the BBC layout, so PicoCalc key legends for symbols will be wrong. | Human's call in phase 0: accept it, or remap symbol keys by character. |
| Which chords open F11 and F12, and which key is COPY? | Human's call in phase 0. |
| Is the Pico 2's own USB port reachable in the case for a USB CDC console? | Human checks; UART0 is the default either way. |
| What drives GPIO 8, where frank-micro probes for PSRAM on RP2350A? | Disable the probe on `pc` regardless. |
| MODE 0 and MODE 3 lose half their columns at 320 wide. | Known and accepted for the Must set; F13 is the follow-up. |

## Sources

- [adq/frank-micro](https://github.com/adq/frank-micro), branch `fruit-jam-port`, last commit 2026-08-29: `CLAUDE.md`, `docs/FRUIT-JAM.md`, `docs/HANDOFF.md`, `docs/INVESTIGATION.md`
- [rh1tech/frank-micro](https://github.com/rh1tech/frank-micro), `main`, last commit 2026-06-19
- [kilograham/b-em](https://github.com/kilograham/b-em), branches `pico` and `pico2`
- [clockworkpi/PicoCalc](https://github.com/clockworkpi/PicoCalc): the helloworld and multi-booter sample code, the keyboard firmware, the v2.0 mainboard schematic and the ST7365P datasheet
