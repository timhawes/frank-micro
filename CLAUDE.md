# frank-micro: orientation for contributors

Read this before changing anything. It exists because the repository contains a
large amount of source that looks live and is not, and one document that
describes a different emulator.

A full audit with citations is in `docs/INVESTIGATION.md`. This file is the
working orientation; that one is the evidence. A validated plan for porting to
the Adafruit Fruit Jam is in `docs/FRUIT-JAM.md`. Decisions taken, dead ends
already explored and the list of things that still need hardware are in
`docs/HANDOFF.md`; read it before re-deriving anything about the port.

---

## 1. What this project is

`frank-micro` is a BBC Microcomputer emulator, Model B and Master 128, running on
the Raspberry Pi Pico 2 (RP2350). It outputs HDMI video with audio embedded in
the HDMI stream, reads disc images from a FAT32 SD card, and takes input from a
PS/2 keyboard, NES or SNES gamepads, or USB HID devices.

Four boards are supported: `m1` Murmulator 1.x, `m2` Murmulator 2.0, `z0`
Waveshare RP2350-PiZero, and `fj` Adafruit Fruit Jam. The Fruit Jam differs from
the other three in enough ways to get its own section; see section 12.

A fifth, `picocalc` ClockworkPi PicoCalc with a Pico 2 or Pico 2 W, is **being
ported, a phase at a time**. See section 13 and `docs/PICOCALC.md`.

The emulation engine is **B-em** by Tom Walker, vendored in `src/bem/`. The parts
that make it fit on a microcontroller come from Graham Sanderson's Pico fork of
B-em: the Thumb-assembly 6502 core, the raw-row rasteriser, the sector-streaming
disc loader, and the `x_gui` display abstraction. The work original to this
repository is the platform layer in `src/frank/` and `drivers/`.

The port is thin and disciplined. Local modifications to vendored B-em code total
about 104 lines across six files, which keeps an upstream rebase feasible. Keep
it that way: new work belongs in `src/frank/`, not in patches to `src/bem/`.

### It is not a beebjit port

The project started as a port of **beebjit**, a different emulator, and was
rewritten onto B-em in commit `2c71dad`. The beebjit core was deleted. Some of
its platform layer and its documentation were not.

If you encounter any of the following, you are looking at the abandoned first
architecture and it does not apply:

- the name beebjit
- a path under `src/micro/`
- `k_cpu_mode_interp`, `inturbo`, a JIT, `accurate_flag`,
  `bbc_cycles_timer_callback`, `wakeup_rate`
- any file in `src/Pico/` other than `ui_draw.c`, `ui_font.c`, `ui_draw.h` and
  `fpu_enable.h`

## 2. Do not follow `PERFORMANCE.md`

`PERFORMANCE.md` is stale and will waste your time. It describes the project as a
beebjit port, directs you to edit files that are not compiled, and states that
the firmware runs at 28 percent of real time as the outstanding problem. That was
solved in commit `c149034` by integrating the Thumb 6502 core, which the document
does not mention. Its "no overclocking" rule is also contradicted by the
composite driver, which forces 378 MHz.

The parts still worth keeping are in section 8 below.

## 3. Which files are live

`CMakeLists.txt:400-414` is the authoritative application source list. Check it
before assuming a file matters.

### Compiled

| Layer | Location | Role |
|---|---|---|
| Application and port | `src/frank/`, 11 `.c` files | `main()`, board and clock init, SD init, the frame loop, PS/2 to BBC key matrix, the F11 and F12 overlays, settings and INI persistence, disc mounting, screenshots, the audio bridge |
| Overlay drawing | `src/Pico/ui_draw.c`, `ui_font.c` | 8bpp framebuffer primitives and a 6x8 font |
| 6502 CPU | `src/bem/thumb_cpu/` | Thumb-assembly core plus an event-driven hardware scheduler. On by default, `CMakeLists.txt:312` |
| Emulation core | 39 files in `src/bem/`, `CMakeLists.txt:345-383` | CRTC, Video ULA, System and User VIA, WD1770, SN76489, memory, model handling |
| Pico emulation glue | `src/bem/pico/video/`, `pico/audio/`, `pico/sector_read/`, `pico/roms/`, `pico/stub_allegro5/` | Rasteriser, audio shim, sector streaming, embedded ROMs, Allegro replacement |
| Platform drivers | `drivers/` | Three video variants, three audio backends, SD, PS/2, NES pad, USB HID, PSRAM, crash handler |

Roughly 13,000 of the repository's 180,000 tracked lines are actually compiled.

### Not compiled, and it cannot be

32 of the 34 files in `src/Pico/` are never built and **will not compile if you
add them**. They include beebjit headers that no longer exist: `bbc.h`,
`bbc_options.h`, `cpu_driver.h`, `interp.h`, `inturbo.h`, `jit.h`, `render.h`,
`state.h`, `state_6502.h`, `teletext.h`, `util.h`, `wd_fdc.h`, `debug.h`, seven
`disc_*.h` headers and ten `os_*.h` headers.

Four shadow the live implementation by name. When you search for a feature you
will find two candidates; the live one is always the `frank_` file.

| Dead | Live |
|---|---|
| `src/Pico/micro_ui.c` | `src/frank/frank_ui.c` |
| `src/Pico/micro_settings.c` | `src/frank/frank_settings.c` |
| `src/Pico/micro_loader.c` | `src/frank/frank_loader.c` |
| `src/Pico/micro_serial_console.c` | `src/frank/frank_console.c` |

Two are worse than dead. `micro_settings.c` uses the **same** `/micro/micro.ini`
path as the live code but a different schema. And `micro_serial_console.c`
defines the line-based protocol (`PING`, `DISK A INSERT`, `SPEED`) that both
`tools/micro_console.py` and `PERFORMANCE.md` document as if it were live; the
real console takes single bytes only.

`tools/micro_console.py` is frank-cpc's script, speaks that dead protocol, and
cannot work. Do not use it as a reference. Note that not everything in `tools/`
is dead: `tools/tlv320_clocks.py` is live and current, and is where the Fruit Jam
audio codec's clock arithmetic lives. `tools/mkssd.py` builds DFS disc images,
and `tools/mkmode7test.py` uses it to build a MODE 7 test disc.

### Not compiled, vendored

About 31,000 lines omitted entirely: `src/bem/NS32016/` (32016 second processor),
`src/bem/resid-fp/` (SID), `src/bem/darm/` (ARM disassembler),
`src/bem/mc6809nc/` (6809). A further roughly 33,000 lines at the `src/bem/` top
level, including `z80.c`, `x86.c`, `65816.c`, `arm.c`, `vdfs.c`, `debugger.c`,
`savestate.c` and the root `video.c`.

The whole `src/bem/pico/x_gui/` implementation is dead. `x_gui.h` is used purely
as an interface contract, and `src/frank/frank_gui.c` and `frank_audio.c` supply
the hooks.

### Dead files among the live ones

- `frank_disc_preload()`, `src/frank/frank_disc.h:19` and `frank_disc.c:209`.
  Never called. Still hardcodes a Prince of Persia path at `frank_disc.c:30`.
- `drivers/HDMI_vga_hstx.c`. In no source list, and it would collide with
  `HDMI_vga.c` if added. It also **cannot** compile: it includes `disphstx.h`
  from the third-party DispHSTX library, which is not vendored anywhere in this
  repository. It targets analogue VGA for a 360x240 Amstrad CPC framebuffer, so
  it is not a starting point for an HSTX driver either.
- `HAS_HSTX`, defined at `src/board_m2.h:21` and used nowhere in the tree.
  Nothing in frank-micro touches the HSTX peripheral, including the
  `frank-hdmi-sound` submodule. Ignore the claim at `src/board_m2.h:12-13` that
  HSTX HDMI is the default video and audio path; it is inherited from frank-cpc
  and is false. See `docs/INVESTIGATION.md`.
- `drivers/sdcard/pio_spi.c`. Every call site is behind `#ifdef SDCARD_PIO`, which
  is never defined.
- `drivers/tv/CMakeLists.txt`, `src/bem/pico/CMakeLists.txt` and
  `src/bem/thumb_cpu/CMakeLists.txt` are never included. Note the consequence:
  `src/bem/pico/memmap_b-em.ld` is **not** used, so the build takes the SDK
  default linker script and default 2 KB stacks.
- The `config_values[]` table at `src/bem/pico/stub_allegro5/al_stub.cpp:314-402`.

## 4. A compiled file does not mean a live feature

This is the easiest way to reach a wrong conclusion here.

`CMakeLists.txt:476-486` disables more than thirty B-em features through
`NO_USE_*` macros while their `.c` files stay in the build and reduce to stubs.
Sixteen files are in that state, including `adc.c`, `i8271.c`, `ide.c`, `scsi.c`,
`csw.c`, `uef.c`, `fdi.c`, `pal.c`, `joystick.c`, `mouse.c`, `music5000.c` and
`ddnoise.c`.

Before concluding a feature exists, grep `CMakeLists.txt` for `NO_USE_` plus its
name.

### Known limitations, none of them in the README

- **All disc images are read-only.** `NO_USE_DISC_WRITE` at `CMakeLists.txt:486`,
  and `src/bem/sdf-acc.c:419-421` answers every write with `fdc_writeprotect()`.
  `*SAVE`, `*SPOOL`, game saves and high-score tables all fail.
- **The Intel 8271 is not emulated.** `NO_USE_I8271` at `CMakeLists.txt:478`
  wraps the whole of `src/bem/i8271.c`. Only the WD1770 is live. A model
  selecting `FDC_I8271` is silently routed to the WD1770 by
  `src/bem/thumb_cpu/src/cpu_mem.c:419-446`.
- **MODE 0 and MODE 3 lose half their horizontal resolution, except under
  HSTX.** `blit_row()` in `src/frank/frank_gui.c` point-samples 640 engine
  pixels into a 320-wide framebuffer. Lossless for the 40-column modes, where
  the engine doubles each pixel; lossy for the 80-column ones, where text
  aliases badly. The `HSTX` build keeps all 640, so this and the 16-line clip
  below are both properties of the PIO video path rather than of the emulator.
- **The bottom 16 scanlines are dropped on the PIO video path.** 640x480p60
  gives a 240-line logical canvas and `frank_hdmi_set_buffer()` clamps a
  256-line framebuffer to it, so framebuffer rows 240 to 255 never reach the
  monitor. Not a property of the emulator: `HSTX` shows all 256.
- **Only 8 physical colours.** `rgb555_to_bbc()` at `src/frank/frank_gui.c:59-64`
  thresholds each channel and returns 0 to 7. No Video NuLA palette.
- **No tube coprocessors, no SID, no save states, no VDFS, no tape loaders.**
  `tape.c` is compiled with tape enabled but both loaders off, which leaves a
  latent null function pointer in `tape_close()`.

## 5. Building

### Prerequisites

1. Pico SDK 2.0 or newer, with `PICO_SDK_PATH` exported.
2. `arm-none-eabi-gcc`.
3. **Ruby, with `erb`.** `CMakeLists.txt:319` requires Ruby to generate the 6502
   dispatch tables from `src/bem/thumb_cpu/gen/gen_asm.rb`, and that script does
   `require 'erb'` at its line 7. Installing Ruby is not always enough: on Arch
   the base `ruby` package omits `erb`, and the build dies at 56 percent with
   `cannot load such file -- erb`. Install `ruby-erb` (or the full Ruby stdlib).
   The README mentions none of this.
4. **A C library for the ARM toolchain.** On Arch, `arm-none-eabi-newlib` is an
   *optional* dependency of `arm-none-eabi-gcc`, so installing the compiler alone
   gives you `cannot find -lg` / `cannot find -lc` at the first link
   (`bs2_default.elf`). Install it explicitly.
5. **The `frank-hdmi-sound` submodule**, for the default video driver:
   `git submodule update --init --recursive`. Note the directory is
   `frank-hdmi-sound` while the repository is `frank-hdmi-audio`, and it tracks a
   moving branch rather than a tag.
6. **The Pico SDK's own submodules**, in the SDK directory:
   `git submodule update --init`. Without `lib/tinyusb` the build still
   **succeeds** but `pico_enable_stdio_usb()` silently does nothing, and you get
   a firmware with no console at all. See the warning under Console output
   below.

### Optimisation levels differ between targets

Nothing sets `CMAKE_BUILD_TYPE`, but the Pico SDK defaults it to `Release`, so
`CMAKE_C_FLAGS_RELEASE` is `-g -O3 -DNDEBUG`. That means assertions are compiled
out and everything is optimised. Verified on a real configured build.

The two tiers differ, and the direction is easy to get backwards:

| Target | Flags | Effective |
|---|---|---|
| `drivers`, `ps2_driver`, `tv_driver` | `-DNDEBUG -O3` | `-O3` |
| `frank-micro` | `... -O3 -DNDEBUG -std=gnu11 -O2 ...` | **`-O2`**, because the explicit `-O2` at `CMakeLists.txt:523` comes last and wins |

So the application layer, the B-em core and the Thumb 6502 CPU are at `-O2`
while the drivers are at `-O3`. If you are chasing emulation speed, that is
where to look first. Confirm before benchmarking:

```bash
grep -- -O build/CMakeFiles/frank-micro.dir/flags.make
grep -- -O build/CMakeFiles/drivers.dir/flags.make
```

### Commands

```bash
./build.sh                                    # default: m2, HDMI with embedded audio
PLATFORM=m1 ./build.sh                        # Murmulator 1.x
PLATFORM=z0 ./build.sh                        # Waveshare RP2350-PiZero
HDMI_DRIVER=HDMI_PIO ./build.sh               # PIO HDMI or VGA, I2S/PWM audio
HDMI_DRIVER=COMPOSITE ./build.sh              # composite PAL/NTSC TV
USB_HID=1 ./build.sh                          # USB HID input
PLATFORM=fj USB_HID=1 ./build.sh              # Adafruit Fruit Jam (needs USB_HID=1)
./release.sh 1.00                             # all eight variants into release/
```

Note `release.sh` still builds only the eight pre-Fruit-Jam variants. `fj` is
deliberately not in `BUILD_MATRIX` until it has been proven on hardware.

Output is `build/frank-micro.uf2`.

**`build.sh` deletes `build/` and rebuilds everything every time**
(`build.sh:20`), which means recompiling about 180,000 lines and regenerating the
6502 tables. For an incremental build after the first configure, use
`cmake --build build -j8`.

`release.sh` sends all build output to `/dev/null` (`:113-119`), so a failed
variant tells you nothing. It also writes `version.txt` before building
(`:91`), so a failed release still consumes a version number.

### Options

| Variable | Default | Effect |
|---|---|---|
| `PLATFORM` | `m2` | `m1`, `m2`, `z0`, `fj` |
| `HDMI_DRIVER` | `HDMI_PIO_AUDIO` | `HDMI_PIO`, `COMPOSITE`, `HSTX`; see below |
| `CPU_SPEED` | `252` | core clock in MHz |
| `USB_HID` | `0` | `1` enables the USB HID host and disables USB CDC stdio |

`build.sh` maps the `USB_HID` environment variable to the `USB_HID_ENABLED` CMake
option; the two names differ.

### The four video builds

| `HDMI_DRIVER` | Drivers | Audio | Constraints |
|---|---|---|---|
| `HDMI_PIO_AUDIO`, default | `HDMI_audio.c`, `HDMI_vga.c` | HDMI-embedded, plus I2S and PWM | Needs the `frank-hdmi-sound` submodule. Audio resampled 31250 to 32000 Hz |
| `HDMI_PIO` | `HDMI.c`, `hdmi_scanline.S`, `HDMI_vga.c` | I2S and PWM only | HDMI or VGA detected at runtime from the ribbon. Note it defines `HDMI_PIO=1` on the `drivers` library but not on `frank-micro`, so application code detects it by the absence of the other two macros |
| `COMPOSITE` | `HDMI_tv.c` plus `tv_driver` | I2S and PWM only | Forces 378 MHz. Fatal on z0 and on fj |
| `HSTX` | `HDMI_hstx.c` plus `drivers/hstx/` | HDMI-embedded, plus I2S | **`fj` only.** 720x576p50, RGB332, single-buffered. Retasks `pll_usb`, so there is no USB-CDC console in this build. Defines `HDMI_HSTX=1` on both the library and the executable, because the framebuffer format changes what application code writes |

The `HSTX` build is the odd one out in a way worth knowing before reading any
drawing code: its framebuffer holds RGB332 colour bytes, not palette indices,
because the HSTX peripheral has no lookup table. `graphics_set_palette()` still
decides what colour an index paints, but it is applied when the pixel is
written, through `FRAMEBUFFER_PIXEL()` in `drivers/HDMI.h`, so it changes
nothing already on screen. See section 6.

### Clock and voltage

`CMakeLists.txt:68-74` derives the regulator voltage from `CPU_SPEED`: 1.50 V
below 300 MHz, 1.60 V from 300, 1.65 V from 504. The default of 252 MHz is not
arbitrary; HDMI PIO timing is calibrated against it at `drivers/HDMI.c:637-638`.
On z0 anything above 300 is silently clamped back to 252 (`:60-62`).

Note that `src/frank/frank_platform.c:308-313` gates the voltage and flash-timing
setup on `CPU_CLOCK_MHZ > 252`, so at the default clock neither runs.

### Console output

`CMakeLists.txt:562` disables UART stdio unconditionally. `:567-571` disables USB
CDC when `USB_HID_ENABLED` is set, and `release.sh:117` sets it for every release
variant. **Release firmware has no console at all.** The README's advice to use
UART instead is wrong as the build stands.

There is a second, quieter way to end up mute. If the SDK's `lib/tinyusb`
submodule is not initialised, `pico_enable_stdio_usb(frank-micro 1)` is a no-op:
CMake prints one warning at configure time, the build succeeds, and the ELF
contains the stdio framework with **no output driver registered at all**. A
default `USB_HID=0` build that should have USB CDC then has nothing. Check with:

```bash
arm-none-eabi-nm build/frank-micro.elf | grep -E 'stdio_(usb|uart)'
```

An empty result means every `printf` in the firmware goes nowhere.

## 6. Runtime architecture

### Core split

Core 0 runs everything: the 6502, the CRTC and ULA, the rasteriser, the sound
generator, keyboard polling, the serial console, the UI overlay, and SD card
reads. Core 1 is the video encoder.

- `HDMI_PIO_AUDIO`: `multicore_launch_core1(frank_hdmi_run_core1)` at
  `src/frank/frank_platform.c:392`.
- `COMPOSITE`: `tv_core1_run` at `drivers/HDMI_tv.c:107`.
- `HDMI_PIO`: core 1 is never launched. The DMA handler is installed by core 0.
  The `..._core1` names in `drivers/HDMI.c:501` and `:506` are misleading
  leftovers.

`SINGLE_CORE` at `CMakeLists.txt:471` documents the intent but has no effect: its
only use is inside a `DISPLAY_WIRE` block that `X_GUI` disables at
`src/bem/pico/video/display.h:17-20`.

### Boot sequence

`main()` is `src/frank/frank_platform.c:306`. In order: FPU enable, voltage and
flash timing for overclocks (`:308-313`), clock set with a 252 MHz fallback
(`:314-315`), stdio (`:317`), a 1.5 second USB CDC enumeration wait when USB HID
is off (`:322`), previous-run crash report (`:325`), crash handler and watchdog
(`:333`), PSRAM probe (`:341`), PS/2 (`:344`), USB HID (`:349`), NES pad (`:355`),
video (`:382-389`), core 1 (`:392`), SD mount (`:401`), halt on no card (`:403`),
settings load (`:409`), then B-em's `main` (`:412`).

### The real frame loop

B-em's `main_run()` calls `al_wait_for_event()`, which always returns a timer
event (`src/bem/pico/stub_allegro5/al_stub.cpp:784-787`), so one iteration is one
50 Hz frame. `m6502_exec()` runs 40,000 cycles, being 20 ms of BBC time at 2 MHz
(`src/bem/thumb_cpu/src/adapter.c:129-137`).

Two frank hooks are called from `al_stub.cpp:708-711`: `frank_keyboard_poll()`
and `frank_perf_tick()`. **`frank_perf_tick()` at
`src/frank/frank_platform.c:75-168` is the application's real main loop.** Per
frame it does first-frame init (`:83-92`), the autoboot state machine (`:95-98`),
the serial console poll (`:102-104`), the watchdog feed (`:107`), and the 50 Hz
pacer (`:120-133`) via `busy_wait_until`. Burning core 0 in the pacer is
deliberate: the asm core is faster than real time at 252 MHz.

### Event-driven hardware, not per-cycle polling

`USE_HW_EVENT` (`CMakeLists.txt:497`) replaces B-em's per-cycle `polltime()` with
a sorted event queue. `advance_hardware()` at
`src/bem/thumb_cpu/src/cpu_mem.c:236-276` pops due events and invokes them.
Registered events include video horizontal total, sound every 16,384 cycles, the
three VIA timers, the ACIA, and the FDC. If you add emulated hardware that needs
timing, register an event rather than polling.

The RP2350 interpolator is used for address decode:
`src/bem/thumb_cpu/src/cpu/cpu.c:281-290` points `interp0->base[0]` at the memory
handler table, which is why `hardware_interp` is linked at `CMakeLists.txt:503`.

### Video path

B-em is built with `X_GUI` and `SINGLE_CORE`, so it rasterises each scanline
synchronously on core 0 into a flat 640-pixel RGB555 row.
`src/frank/frank_gui.c` converts each row to an 8-bit palette index and writes
it, downscaled 2:1 horizontally, into a 320x256 framebuffer that core 1 scans
out.

| Step | Location |
|---|---|
| Framebuffer, double-buffered 320x256 8bpp, 160 KB | `src/frank/frank_gui.c:30` |
| Row start, buffer flip | `frank_gui.c:146-158` (`x_gui_begin_scanline`) |
| Per-row blit | `frank_gui.c:128` (`blit_row`) |
| Teletext MODE 7 blit | `frank_gui.c:94` (`blit_row_teletext`) |
| Screenshot, UI overlay, present | `frank_gui.c:165-180` (`x_gui_end_scanline`) |
| CRTC and ULA timing | `src/bem/pico/video/video.c` |
| Rasteriser | `src/bem/pico/video/display.c`, `pixels.S` |

The buffer flip happens at the **start** of each frame (`frank_gui.c:149`), so
during rendering `current_buffer` is the buffer being written and
`!current_buffer` is the completed one that core 1 displays. Do not "fix" the
apparent inversion in `drivers/HDMI.c:441`; it is correct.

MODE 7 has a dedicated renderer because the generic path looked wrong. It samples
the six SAA5050 glyph columns that survive at full intensity, at offsets
`{0,2,5,8,11,14}` (`frank_gui.c:96`). That is commit `5f6666f`.

`frank_gui.c:39-49` explains why the frame number is encoded into `scanline_id`:
the rasteriser uses it to pad each frame to exactly 256 scanlines, and without it
a program that reprograms the CRTC mid-frame desyncs the accounting.

#### The two PIO video drivers are different designs, not two spellings of one

Both emit TMDS from PIO state machines, and neither uses the RP2350's HSTX
peripheral. Beyond that they share almost nothing, so do not carry an assumption
from one across to the other.

**`HDMI_PIO`, in `drivers/HDMI.c`.** Two state machines and four DMA channels,
with no per-pixel CPU work at all. TMDS is precomputed: `tmds_encoder()` at
`:346` is the real 8b/10b algorithm but runs once at init to fill
`tmds_table[256]` (`:157`), and `graphics_convert_all_palette()` at `:777`
pre-serialises each palette entry into a differential word (`:787`). So encoding
cost is paid per palette change, not per pixel.

The palette lookup is then done by PIO and DMA between them, which is the part
worth understanding before touching this file:

1. `dma_chan` streams a 400-byte scanline of palette indices, 8 bits at a time,
   into `SM_conv`'s TX FIFO (`:660-668`).
2. `SM_conv` turns each index into an address in `conv_color` and pushes it out
   its **RX** FIFO.
3. `dma_chan_pal_conv_ctrl` reads that RX FIFO and writes it into
   `dma_chan_pal_conv`'s `read_addr` register (`:726-732`), so a PIO output
   becomes a DMA read address.
4. `dma_chan_pal_conv` moves 4 words from there into `SM_video`'s TX FIFO
   (`:703-711`).
5. `SM_video` shifts out to 6 data pins, 3 TMDS pairs, with 2 sideset pins as the
   clock pair (`:601-602`, `:631`), at a clkdiv calibrated against 252 MHz
   (`:638`).

Sync and blanking are more byte values in the same line buffer, indices into
control-code entries at `BASE_HDMI_CTRL_INX`. The per-line ISR
`dma_handler_HDMI()` at `:374` only swaps the double-buffered line buffer and
refills the porch bytes. This is why this variant never launches core 1.

**`HDMI_PIO_AUDIO`, the default, in the `frank-hdmi-sound` submodule.** A
PicoDVI-derived design. Three state machines, one per TMDS lane
(`frank_serialiser.c:2-3`), and TMDS encoded **on the CPU, per scanline, in
hand-written assembly**: `frank_tmds.S`, entered through
`tmds_encode_data_channel_8bpp` and `..._16bpp` at `frank_tmds.c:83` and `:108`,
against a table pinned in `SCRATCH_X` (`frank_tmds.c:25`).

That CPU cost is why this variant launches core 1 and `HDMI_PIO` does not. What
it buys is `frank_data_packet.c`: HDMI data islands, small TERC4-encoded packets
inserted into the horizontal blanking interval, which is how audio is embedded in
the stream. Widths are at `frank_data_packet.h:23-26` and `:37`. `HDMI_PIO`
cannot do this, which is why it is I2S and PWM only.

It is also why the audio path resamples to 32,000 Hz rather than sending 31,250
(`CMakeLists.txt:144-150`). An HDMI sink derives its audio clock from N and CTS
values carried in a data island, and those are tabulated per standard sample
rate. 31,250 is not a standard rate.

#### The HSTX driver, `drivers/HDMI_hstx.c` plus `drivers/hstx/`

Not PIO at all. The HSTX peripheral does TMDS encoding in hardware from its own
FIFO, so the data path is DMA to FIFO to pins with neither core in it.

| Step | Location |
|---|---|
| Command lists, sync, data islands, the line IRQ | `drivers/hstx/video_output.c` |
| TERC4 packet and infoframe encoding | `drivers/hstx/hstx_packet.c` |
| Audio packet ring | `drivers/hstx/hstx_data_island_queue.c` |
| `graphics_*` API, clocks, palette, audio push | `drivers/HDMI_hstx.c` |

Three things about it differ from both PIO drivers.

**No palette lookup, so the framebuffer holds colours.** Bytes are RGB332,
laid out `RRRGGGBB`, and the pixel DMA hands them straight to the encoder.
`FRAMEBUFFER_PIXEL()` in `drivers/HDMI.h` is where an index becomes a colour;
it is the identity in every other build. The consequence to remember is that a
palette write does not repaint. That is why the boot error screen's plasma is
static under HSTX (`frank_platform.c`): it animated by rotating the palette.

**No line buffer and no per-line CPU work.** `fb_row_addr[]` resolves every
raster line to a source address once, when the geometry is set, so the line
IRQ writes two DMA registers and returns. Upstream called a scanline callback
here costing about 21 us per line; that is gone. Core 1 runs the IRQ and the
audio task and is otherwise idle.

**The framebuffer is 720x256 and single-buffered.** A video data period is
exactly 720 pixels, so rows are 720 bytes and the 40-pixel pillarbox is black
pixels inside the row. 640 engine pixels are kept, which is what fixes the
MODE 0 and MODE 3 loss in section 4, and 256 rows are doubled onto 512 of the
576 active lines, which is what fixes the 16-line clip. There is no room for a
second 180 KB buffer, so tearing is handled by phase-locking the emulator
frame to vsync in `frank_perf_tick()`; `HSTX_FRAME_LEAD_US` is the tuning
knob.

That has a consequence beyond tearing, and it catches anything drawn over the
emulator picture. Each row is written ahead of the beam, so the
composite-after-the-frame model the overlay uses on the PIO drivers does not
work: the next frame's blit erases the overlay before the beam reaches it and
restores it only after the beam has passed, which shows as the overlay
decaying row by row. `blit_row()` therefore asks `frank_ui_row_span()` which
span of the row the overlay owns and skips it. **On this path, anything drawn
on top of the picture has to be written per row, in step with the blit.**

Clocks are the other thing to know. `clk_hstx` has to be exactly 135 MHz, five
times the 27.0 MHz pixel clock, and `clk_sys` stays at 252 MHz on `pll_sys`.
135 is not an integer divisor of 252 and a fractional divider puts jitter on
the TMDS bit clock, so `hdmi_hstx_clock_init()` retasks `pll_usb` to 135 MHz
and stops `clk_usb` and `clk_adc`. That leaves the native USB controller
without a clock, which costs nothing on the Fruit Jam because the host is
PIO-USB and the console is UART1, but it does mean **no USB-CDC in an HSTX
build.**

**`pll_usb` is also the peripheral PLL, and that is a trap.**
`set_sys_clock_khz()` leaves `clk_peri` attached to `pll_usb`, undivided, at
48 MHz, so retasking that PLL moves the UART and SPI0 clock with it while
`clock_get_hz(clk_peri)` still reports the old value. It presents as an SD
card that will not mount with a card in the slot. `hdmi_hstx_clock_init()`
therefore re-points `clk_peri` at `clk_sys / 3`, 84 MHz, before it touches
`pll_usb`, and it is called from `main()` immediately after
`set_sys_clock_khz()` rather than from `graphics_init()`, because every
divisor downstream has to be computed after the change. See
`docs/HANDOFF.md`.

### Audio path

The SN76489 produces samples at 31,250 Hz. A hardware event every 16,384 cycles
drives `sound_poll_n()` (`src/bem/pico/audio/audio.c:571`), which fills a single
static 882-sample mono buffer and hands it to `give_audio_buffer()` at
`src/frank/frank_audio.c:195`. That is about 28 ms per handoff.

`give_audio_buffer()` branches on the active backend. **It must never block the
producer**, and today two of the three paths can:

| Backend | Path | Note |
|---|---|---|
| I2S | `frank_audio.c:204-220` | native 31250 Hz, mono to stereo, software volume, blocking `i2s_dma_write` |
| PWM | `:222-232` | native, blocking push |
| HDMI | `:235-253` | exact fixed-point resample 31250 to 32000 Hz, flushed in 128-frame blocks |

The 32,000 Hz choice is explained at `CMakeLists.txt:144-150`: real HDMI sinks
mishandle the non-standard 31,250 Hz and drop a sample every few seconds.

Backend switching is live, in `frank_audio_set_driver()` at
`src/frank/frank_audio.c:123`. I2S and PWM share pins on **all three** boards, so
the switch re-muxes them: `i2s_claim_pins()` at `:65`, `pwm_claim_pins()` at
`:71`. Known rough edges: the abandoned backend is never actually stopped, only
silenced; `i2s_quiet()` can spin for up to about 56 ms; and the HDMI resampler
keeps stale state across switches.

### PIO, DMA and interrupt budget

The budget differs between M1/M2/Z0 and the Fruit Jam, because the Fruit Jam has
no PS/2 socket and no NES pad connector but does have a PIO USB host. Check the
right table before adding any PIO consumer.

**On M1, M2 and Z0, PIO1 is exactly full at four state machines in every
configuration**, and one is wasted.

| Block | Claimant | State machines |
|---|---|---|
| PIO0 | video, whichever driver is built | 1 to 3 |
| PIO1 | PS/2 keyboard | 1 |
| PIO1 | PS/2 "mouse" | 1, wasted on M1 and Z0 |
| PIO1 | NES pad | 1 |
| PIO1 | I2S | 1 |

On M1 and Z0 there is no PS/2 mouse, so `src/board_config.h` aliases
`PS2_MOUSE_CLK` to `PS2_PIN_CLK` and the mouse state machine is initialised on
the same pin as the keyboard. Reclaiming it is the cheapest way to free PIO1
capacity. Instruction memory is also near the limit at about 27 of 32 words.

**On the Fruit Jam, PIO1 belongs entirely to the USB host and PIO2 is spoken
for.**

| Block | Claimant | State machines |
|---|---|---|
| PIO0 | video, three TMDS serialisers; **free in an `HSTX` build** | 3, or 0 |
| PIO1 | PIO-USB host, and its transmit program must be at instruction offset 0 | 3 |
| PIO2 | I2S, once the codec driver exists | 1 |

Nothing is spare there either, and PIO1 in particular cannot be shared: the
library places its transmit program at offset 0, so the block has to be one that
nothing else has programmed. PIO1 rather than PIO2 for USB is forced, not chosen;
see section 12.

The `HSTX` build frees PIO0 completely, because HSTX is a peripheral rather
than a PIO program. Nothing claims those four state machines today.

DMA in an `HSTX` build: video takes channels 0 and 1 and `DMA_IRQ_1`, which is
the same interrupt the PIO drivers use. Channels 0 and 1 are safe because
PIO-USB is pinned to 7, I2S hardcodes 10 and 11 and PWM hardcodes 8 and 9. Note
that the vendored driver used `DMA_IRQ_0` upstream and was moved, because
`DMA_IRQ_0` is I2S here.

DMA: the video drivers claim dynamically; **I2S hardcodes channels 10 and 11**
(`drivers/audio.c:47-48`) and **PWM hardcodes 8 and 9**
(`drivers/pwm_audio/pwm_audio.c:67-68`). Interrupts: `DMA_IRQ_1` video,
`DMA_IRQ_0` I2S, `DMA_IRQ_2` PWM, `PIO1_IRQ_0/1` PS/2, `TIMER0_IRQ_2` composite.
The comments in `drivers/audio.c:17`, `:135` and `:176` name the wrong IRQ in
three places; the code is right and the comments are wrong.

### Storage layout

| Path | Contents | Defined at |
|---|---|---|
| `/micro/disk/` | disc images | `src/frank/frank_loader.c:20` |
| `/micro/micro.ini` | settings | `src/frank/frank_settings.c:364` |
| `/micro/screenshot/` | `BBC_NNNN.BMP`, 8-bit BMP | `src/frank/frank_screenshot.c:57` |

Undocumented behaviour worth knowing: with no `disk_a` set, the loader falls back
to `/micro/disk/drivea.ssd` and `driveb.ssd`
(`src/frank/frank_loader.c:133`); mounting into drive 0 triggers an automatic
SHIFT-BREAK (`src/frank/frank_ui.c:294-296`); the browser caps at 200 entries and
skips filenames of 64 characters or more, silently
(`src/frank/frank_loader.c:66`, `:74-75`).

### Memory budget

Measured on linked builds of every variant:

| Platform | Driver | Flash | of 4 MB | RAM | of 512 KB |
|---|---|---|---|---|---|
| m2 | `HDMI_PIO_AUDIO` | 353,340 | 8.4% | 334,276 | 63.8% |
| m2 | `HDMI_PIO` | 348,916 | 8.3% | 297,268 | 56.7% |
| m2 | `COMPOSITE` | 361,288 | 8.6% | 389,172 | 74.2% |
| m1 | `HDMI_PIO_AUDIO` | 353,340 | 8.4% | 334,276 | 63.8% |
| m1 | `HDMI_PIO` | 348,908 | 8.3% | 297,268 | 56.7% |
| m1 | `COMPOSITE` | 373,740 | 8.9% | 393,000 | 75.0% |
| z0 | `HDMI_PIO_AUDIO` | 366,128 | 8.7% | 336,136 | 64.1% |
| z0 | `HDMI_PIO` | 361,208 | 8.6% | 299,128 | 57.1% |
| fj | `HDMI_PIO_AUDIO` | 387,464 | 2.3% of 16 MB | 352,772 | 67.3% |
| fj | `HDMI_PIO` | 382,620 | 2.3% of 16 MB | 315,764 | 60.2% |
| fj | `HSTX` | 381,756 | 2.3% of 16 MB | 379,904 | 72.5% |

The `HSTX` figure is the tight one on the Fruit Jam, but it still sits below
`COMPOSITE` on the other boards, so the section's advice to budget against
`COMPOSITE` is unchanged. Where its RAM goes, over `HDMI_PIO`: a 720x256
framebuffer is 180 KB against 160 KB for two 320x256 ones, and the audio
data-island ring is 36 KB (`DI_RING_BUFFER_SIZE` in
`drivers/hstx/hstx_data_island_queue.c`, statically allocated so it shows up
here rather than in the heap).

Plus, for the default m2 `HDMI_PIO_AUDIO` build: `SCRATCH_X` 2,404 B of 4 KB
(58.7%), `SCRATCH_Y` 2,688 B of 4 KB (65.6%), `XIP_RAM` unused.

**Budget new allocations against `COMPOSITE`, not the default.** It is the tight
one at 75.0% of RAM, about 96 KB above `HDMI_PIO`, because of the 76.8 KB
`tv_frame` buffer at `drivers/HDMI_tv.c:70`. It also runs at 378 MHz and 1.60 V.

**The 4 KB scratch banks are the real constraint**, both about two thirds full,
with far less headroom than the RAM figure suggests. Flash is under 9% in every
variant.

Of the RAM, two 320x256 framebuffers at 160 KB
(`src/frank/frank_gui.c:30`), 64 KB BBC RAM, 64 KB Master sideways RAM, two
640-entry work rows. Composite adds a 76.8 KB `tv_frame`
(`drivers/HDMI_tv.c:70`). The audio layer holds about 11 KB of static buffers
regardless of the selected backend. PSRAM is optional and, in practice, is used
only for FatFs long-filename work buffers via
`drivers/fatfs/ffsystem.c:24`.

## 7. Where to change what

| To change | Go to |
|---|---|
| Boot sequence, clocks, SD init, the frame loop and pacer | `src/frank/frank_platform.c` |
| The F12 settings menu or F11 disc browser | `src/frank/frank_ui.c` |
| A new setting, or INI persistence | `src/frank/frank_settings.c` and `.h` |
| Disc image browsing and directory walking | `src/frank/frank_loader.c` |
| Mounting, ejecting, SHIFT-BREAK autoboot, sector reads | `src/frank/frank_disc.c` |
| PS/2 or gamepad to BBC key matrix | `src/frank/frank_keyboard.c` |
| Audio backend behaviour or the live switch | `src/frank/frank_audio.c` |
| Scanline conversion, palette, MODE 7 | `src/frank/frank_gui.c` |
| Screenshot capture and BMP writing | `src/frank/frank_screenshot.c` |
| Overlay text and box drawing | `src/Pico/ui_draw.c`, `ui_font.c` |
| Which model boots, and its ROMs | `src/bem/pico/stub_allegro5/al_stub.cpp:286-311` |
| Pin assignment for a board | `src/board_m1.h`, `board_m2.h`, `board_z0.h`, `board_fj.h` |
| Whether a board has PS/2, a NES pad, PWM audio | the `HAS_*` macros in its `src/board_*.h`, see section 12 |
| HDMI, VGA or composite output | `drivers/HDMI*.c`, `drivers/tv/` |
| I2S or PWM output | `drivers/audio.c`, `drivers/pwm_audio/` |
| Emulated hardware timing | register a `hw_event`, see section 6 |

## 8. Hardware constraints

The first four are carried over from `PERFORMANCE.md`. They were written from
experience with the earlier firmware and have **not been re-verified** against
the current build, but the hardware behaviour is unchanged and the cost of
ignoring them is high.

1. **Ask the user to enter BOOTSEL mode before every flash.** Bad firmware
   reliably drives the RP2350 into a Cortex-M33 hardware lockup at
   `PC=0xEFFFFFFE` that survives reset, flash erase and SRST. Do not fight the
   lockup over SWD. Have the user hold BOOTSEL while plugging in USB, or BOOTSEL
   plus a tap of RESET, then flash. Note that `flash.sh:22` uses
   `picotool load -f` while `PERFORMANCE.md` insists on `-x`; the two disagree
   and nobody has reconciled them.
2. **Diagnose over SWD, not by polling the serial port.** Attach OpenOCD and GDB
   and read the PC and stack directly. `CFSR` is at `0xe000ed28`, `BFAR` at
   `0xe000ed38`. Remember that release builds have no serial console at all.
3. **Do not raise the clock to buy performance.** HDMI PIO timing is calibrated
   against 252 MHz. Composite is the one exception and sets 378 MHz itself.
4. **Core 1 must stay resident in RAM and must never block.** It is scanning out
   video in real time.
5. **PSRAM is optional and not required.** Detected and initialised if present
   (`drivers/psram_init.c`). Keep emulation hot paths out of it. A stock Pico 2
   with no PSRAM is a supported configuration. On **M1 only**, the PSRAM
   chip-select pin (GPIO 8) is also HDMI D0 minus, and `psram_init` muxes the pin
   to `XIP_CS1` before probing and leaves it there on failure. It survives only
   because `graphics_init()` re-muxes later.
6. **Do not let `printf` block core 0.** `CMakeLists.txt:576` sets
   `PICO_STDIO_USB_STDOUT_TIMEOUT_US=0` so USB CDC writes drop characters rather
   than stalling. Commit `82469e5` exists because this caused audio dropouts.
   `FRANK_PERF_LOG` is off by default for the same reason.
7. **Keep work out of the per-scanline hook.** `x_gui_end_scanline()` currently
   performs the screenshot write there, an 84 KB SD write inside the video path.
   Do not add to it.

## 9. Verifying a change

There is no CI and there are no tests. Every check is manual and on hardware.

**Assume you cannot test every board.** Four platforms are supported and few
people will have all four in front of them, so treat the ones you cannot run as
frozen:

- Prefer additive, platform-gated changes to shared code over changes in place.
- Where shared code must change in place, write down why the other boards are
  unaffected, next to the change. "The preprocessor output is identical for
  m1, m2 and z0" is the kind of argument that counts; "it should be fine" is not.
- Say which boards you actually tested on. Do not describe a change as verified
  on a board you did not run it on.

The cheapest useful check needs no board: configure and compile every variant.
That alone catches broken source lists, missing dependencies and dead-file
mistakes. **All twenty pass as of 2026-08-25**, so a failure means you broke
something. For reference, the `fj` figures at that point were 387,464 bytes of
flash and 352,772 of RAM with `USB_HID=1`, against 368,044 and 337,272 for the
default `m2` build; flash is on a 16 MB part here rather than 4 MB.

Sweep `USB_HID` too: it changes which input drivers and which stdio backend are
compiled in, so it is a second dimension and not a detail.

The sweep also covers `picocalc`, which has exactly one variant: `HDMI_DRIVER=LCD_SPI`,
`USB_HID=0`. The extended loop is in `docs/PICOCALC.md` section 11. `picocalc` with any
other driver, any other platform with `LCD_SPI`, and `picocalc` with `USB_HID=1` all
fail at configure time, and that is intended.

```bash
for p in m2 m1 z0 fj; do for d in HDMI_PIO_AUDIO HDMI_PIO COMPOSITE; do
  [ "$p" = z0 ] && [ "$d" = COMPOSITE ] && continue
  [ "$p" = fj ] && [ "$d" = COMPOSITE ] && continue
  for u in 0 1; do
    PLATFORM=$p HDMI_DRIVER=$d USB_HID=$u ./build.sh \
      || echo "FAILED: $p $d hid=$u"
  done
done; done
```

Then check the thing that fails silently, per section 5:

```bash
arm-none-eabi-nm build/frank-micro.elf | grep -E 'stdio_(usb|uart)'
```

An empty result means the firmware has no console. That is expected for `m1`,
`m2` and `z0` with `USB_HID=1`, and is a bug for `fj`, whose console is on
UART1.

On hardware, exercise the paths that cross driver boundaries:

- Cold boot to the Acorn MOS screen.
- A MODE 7 teletext screen and at least one bitmap mode; they take different blit
  paths in `frank_gui.c`.
- Mount a disc from the F11 browser and SHIFT-BREAK autoboot it.
- All three audio backends, switched live from F12, checking that the backend you
  left is not still driving its pins.
- PS/2 keyboard, including F11, F12, Print Screen and Ctrl+Alt+Del.
- A screenshot, then confirm the BMP on the card.
- Boot with **no SD card**, and confirm the error screen stays up rather than
  rebooting every few seconds. It currently does not; see
  `docs/INVESTIGATION.md` finding 4.
- Several minutes of running, to catch a watchdog reset or lockup.

Report what you actually ran. If you could not test on hardware, say so.

## 10. Conventions

- **Licence headers.** The port's own files carry a `frank-micro` header with
  `SPDX-License-Identifier: GPL-3.0-or-later`. Vendored trees keep their upstream
  headers; do not stamp over them.
- **27 files under `drivers/` currently carry the wrong project header**, naming
  `frank-cpc` and its URL. If you touch one of those files, fix its header. Two
  vendored components, `drivers/tv/` and `drivers/ps2/`, have no attribution at
  all. See `docs/INVESTIGATION.md` findings 7 and 8.
- **Licence compatibility.** The vendored B-em code is GPL-2.0-**or-later**,
  which is what permits the GPLv3 combination. Do not vendor GPL-2.0-only code.
  Separately, the repository contains copyrighted Acorn ROM images and third-party
  disc images that cannot be licensed under GPLv3; be aware before publishing
  anything derived from the tree.
- **Commit messages.** `.githooks/commit-msg` rejects AI attribution trailers,
  but it is **not active**: `core.hooksPath` is unset and `.git/hooks/commit-msg`
  does not exist, so nothing runs it unless someone configures
  `git config core.hooksPath .githooks`. Treat the intent as the house rule
  regardless: no `Co-Authored-By` or `Generated with` lines naming an AI tool.
- **Author identity.** File headers give `xtreme@rh1.tech`; all commits are
  authored as `xtreme@rh1.ru`. Ask before changing either.
- **Do not add to `src/bem/`.** New work belongs in `src/frank/`. The vendored
  divergence is currently about 104 lines and is worth keeping small.

## 11. Known problems

Full detail and citations in `docs/INVESTIGATION.md`. Ranked.

| Area | Problem |
|---|---|
| Build | Two undocumented prerequisites beyond the README: Ruby's `erb`, and the SDK's own `lib/tinyusb` submodule, whose absence silently produces firmware with no console |
| Docs | `PERFORMANCE.md` describes a different emulator |
| Dead code | 32 unbuildable files in `src/Pico/`, two of which shadow live behaviour |
| Boot | The no-SD error screen reboots on the watchdog instead of halting |
| Emulation | `src/bem/acia.c:28` passes the wrong object to the ACIA timer callback |
| Licensing | Copyrighted ROM and disc images conflict with GPLv3 redistribution |
| Attribution | 27 driver files name the wrong project; `drivers/tv/` has no licence at all |
| Build | Ruby is required and undocumented |
| Build | Release firmware has no console; the README says to use UART |
| Hygiene | No `-Wall` anywhere, and thirteen `-Wno-` suppressions |
| Robustness | Settings save truncates first, ignores every write result, and runs on every keypress |
| UX | A Model change is written to the card before the user confirms it |
| Input | Gamepad keys stick if the preset changes mid-press |
| Drawing | `ui_draw` primitives do not clip; `put_pixel` has no bounds test at all |
| Drivers | `HDMI_vga.c:432-436` re-bases PIO GPIO to 32, which `HDMI.c:534-538`'s own comment calls invalid on RP2350; unreachable today only because the Z0 forces `SELECT_VGA = false` |
| Drivers | The I2S pin-mux maps any non-pio0 block to `GPIO_FUNC_PIO1` (`drivers/audio.c:99`, `frank_audio.c:66-68`); wrong for PIO2, latent until anything uses it |
| Concurrency | PS/2 ring buffers are drained from both an interrupt and a poll path with no masking |
| Resources | PIO1 is exactly full on M1, M2 and Z0, with one state machine wasted on a nonexistent mouse. On the Fruit Jam all three PIO blocks are spoken for |
| Tooling | `tools/micro_console.py` is another project's script and speaks a dead protocol |
| Build | Application and emulation core build at `-O2` while drivers build at `-O3`; undocumented and possibly unintended |
| Process | No CI, no tests |

**Fixed 2026-11-02 in shared code, confirmed on the PicoCalc only.** Every
board compiles with them; none other has been run. A Fruit Jam tester is
lined up. Details and the test are in `docs/HANDOFF.md`.

- **The Master's shadow screen leaked into main RAM.** `CPURun()` in
  `src/bem/thumb_cpu/src/cpu/cpu.c` reset the interpolator to the normal
  memory map once per frame, so MOS VDU code caught by a frame boundary wrote
  shadow-screen data over main RAM. It crashed the Master Welcome disc's
  Control Panel and left BREAK unable to clear the screen.
- **The Master's clock ran at a tenth of real speed.** The stub `time()` in
  `src/bem/pico/stub_allegro5/include/allegro5/allegro.h` divided by 10^7.


## 12. The Fruit Jam, and what makes it different

`PLATFORM=fj`, Adafruit product 6200, RP2350B, 16 MB flash, 8 MB PSRAM. It
differs from the other three boards in enough ways to be worth its own section.

Confirmed working on hardware as of 2026-08-25: cold boot to the Master 128 MOS
screen over DVI, disc images from SD, HDMI-embedded audio, a USB keyboard behind
the onboard hub, and sound from the headphone jack through the codec. Not yet
exercised: MODE 7 versus a bitmap mode, mounting a disc and SHIFT-BREAK autoboot,
screenshots, `micro.ini` persistence, the onboard speaker, the no-SD-card path,
and stability over hours.

The full plan is `docs/FRUIT-JAM.md`; what was decided and found while building
it is in `docs/HANDOFF.md`.

Pins are in `src/board_fj.h` and come from the Pico SDK's own
`boards/adafruit_fruit_jam.h`, cross-checked against Adafruit's published
schematic. The DVI pins are GPIO 12 to 19 in exactly the order `board_m2.h`
already declares, polarity included, so the video drivers needed no change.

### Build it with `USB_HID=1`

There is no PS/2 socket and no NES pad connector, so a USB keyboard is the only
input the board has. A `USB_HID=0` build boots and displays but has no keyboard
at all, which is useful for bring-up and useless otherwise.

The two USB-A sockets sit behind a CH334F hub whose upstream pair is on GPIO 1
and 2. Those are ordinary PIO pins, not the RP2350's native USB controller, and
**the USB-C connector cannot be made a host**: both its CC pins carry 5.1K to
ground, which is sink termination, and nothing on the board can source 5 V onto
that connector under firmware control. So PIO-USB is not one option among
several, it is the only input path.

Two consequences in the code:

- The hub and both sockets are on a switched 5 V rail. `frank_platform.c` drives
  `USB_HOST_5V_EN_PIN` high and waits before the host stack initialises. Without
  that the sockets are dead and nothing enumerates.
- `Pico-PIO-USB` is a submodule at `lib/Pico-PIO-USB`, pinned at `5a37a66`.
  **It must be a version with RP2350 support**, which the 0.6.1 that TinyUSB
  0.18.0 names in `tools/get_deps.py` does not have. Without the RP2350-E9
  workaround in `pio_usb_bus_get_line_state()` the host never detects a device
  on the port, which looks exactly like a dead keyboard on a board that is
  otherwise working. See `docs/HANDOFF.md`. All the build glue comes from the
  SDK's own TinyUSB, so `CMakeLists.txt` only has to set `PICO_PIO_USB_PATH`,
  and it has to do so before `pico_sdk_init()`.

### The console is on UART1, GPIO 8 and 9

This is the one platform where a `USB_HID=1` build still has a console.
`CMakeLists.txt` turns USB CDC off whenever USB HID is on, which is right for
the other three boards because there USB HID owns the native controller. Here it
does not: the host is on PIO, and `hcd_rp2040.c` is compiled out entirely. So
UART stdio is enabled for `fj` and you attach a USB-serial adapter to the 2x16
header.

A CDC device on the native port alongside the PIO host is possible and is not
blocked by anything structural, but it needs a TinyUSB device stack and a host
stack in one binary. Not attempted. See `docs/HANDOFF.md`.

### Capability macros, which finally do something

`HAS_PS2` and `HAS_NESPAD` are defined in `board_m1.h`, `board_m2.h` and
`board_z0.h` and absent from `board_fj.h`, which declares no PS/2 or pad pin
numbers at all. Five sites are guarded on them, in `board_config.h`,
`drivers/ps2/ps2kbd_wrapper.c`, `frank_platform.c` and `frank_keyboard.c`.

**Add a board by omitting a capability, not by inventing pin numbers for
hardware that is not there.** Before this, the `HAS_*` macros were declared in
three headers and read nowhere.

### The `HSTX` video build

`HDMI_DRIVER=HSTX` is Fruit-Jam-only and is rejected at configure time
elsewhere. It exists because the PIO video path drops the BBC's bottom 16
scanlines and point-samples away half the horizontal resolution of MODE 0 and
MODE 3; both are properties of that path and both go away here. Section 6 has
the design.

What changes if you build it:

- **No USB-CDC console, whatever `USB_HID` is set to.** `pll_usb` is retasked
  to 135 MHz for `clk_hstx`, so the native USB controller has no clock. Use
  the UART1 console on GPIO 8 and 9.
- **The framebuffer holds RGB332 colours, not palette indices.** Anything that
  writes a pixel has to go through `FRAMEBUFFER_PIXEL()`. Adding a new drawing
  routine that stores a bare index will paint the wrong colour and will do it
  silently.
- **PWM audio is still absent and HDMI audio is still available**, so the F12
  menu offers the same HDMI and I2S pair as the default build.
- **The `frank-hdmi-sound` submodule is not needed.**

Confirmed on hardware 2026-08-28: video output over DVI, the SD card,
the F11 and F12 overlays, mounting a disc and SHIFT-BREAK booting it,
screenshots, every display MODE including teletext, and **both of the faults
this build exists to fix**. MODE 0 and MODE 3 render their 80 columns
sharply, and all 256 scanlines reach the monitor.

MODE 7 is worth calling out: this build deletes the dedicated teletext
renderer that the PIO path needs, because at 640 pixels the SAA5050 cells
arrive whole and there is nothing to reconstruct. That is confirmed, colour,
double height and separated mosaic graphics included.

The no-SD-card error screen is confirmed too, including its lack of
animation: it animated by rotating the palette, which repaints nothing here,
so this build draws it once and leaves it. Note that the screen still reboots
on the watchdog rather than halting, which is `docs/INVESTIGATION.md`
finding 4 and predates this work.

Not yet exercised: audio on either backend, and stability over hours.

### What the board does not have

- **No composite TV.** No video DAC on the DVI pins, so
  `HDMI_DRIVER=COMPOSITE` is rejected at configure time. That also removes the
  variant `CLAUDE.md` section 6 tells you to budget RAM against, so on this
  board the tight variant is `HDMI_PIO_AUDIO`.
- **No VGA ribbon**, so no runtime HDMI-versus-VGA detection. Nothing had to
  change for this: `frank_platform.c` already forces `SELECT_VGA = false` in
  every `HDMI_PIO_AUDIO` build.
- **No PWM audio output.** The headphone jack and the onboard speaker are both
  behind a TLV320DAC3100 codec. `PWM_PIN0` and `PWM_PIN1` are still defined,
  because `pwm_audio.c` needs them to compile, but they point at free header
  pins and the backend is left out of the F12 menu by `frank_settings.c`.
- **Two working audio backends, and neither is PWM.** "HDMI" rides in the DVI
  stream and comes out of the monitor; "I2S" goes through the TLV320DAC3100 to
  the headphone jack and the onboard speaker. Both are confirmed on hardware.
  HDMI is the default and needs nothing on the board.

  The codec is `drivers/tlv320dac3100.c`. Two things about it are worth knowing
  before touching it. Its clock constants come from `tools/tlv320_clocks.py` and
  must not be copied from another project, because the register block both
  Adafruit ports use is out of spec at every rate frank-micro produces. And the
  F12 volume drives its **analogue** output stage rather than dividing the
  samples, so the digital path keeps all sixteen bits; see section 8 constraint 6
  for why nothing in that driver may be called from the audio producer path.
- **No hot-plug detect.** The connector's `HOTPLUG` pin reaches no GPIO, so
  firmware cannot tell whether a monitor is attached.

### Two hazards worth knowing before touching the board

- `PERIPH_RST` on GPIO 22 has a 10K pull-up, so the codec and the ESP32-C6 both
  leave reset with no firmware action. **The hazard is asserting that line, not
  forgetting to release it**, and asserting it resets both parts.
- GPIO 23 is a three-way net: the codec's `GPIO1`, the ESP32-C6's `IO9/BOOT9`,
  and the `ESP_BOOT` header. The codec bring-up sequence in both Adafruit
  projects configures the codec's `GPIO1` as an output, which on this board can
  drive the ESP32's boot strap into download mode. Decide about that register
  write rather than copying it.

Bricking is not a risk: `BTN0` is wired as a standard BOOTSEL, so holding it
while plugging in USB always recovers the board. To flash, do that, then copy
`build/frank-micro.uf2` to the `RPI-RP2` drive. `picotool` and `openocd` are not
installed on this machine, so `./flash.sh` will not run as it stands.


## 13. The PicoCalc, in progress

`PLATFORM=picocalc`: a ClockworkPi PicoCalc, mainboard v2.0, with a **Pico 2 or Pico
2 W** (RP2350A, 4 MB flash). Phase 0, the plan, was written on 2026-10-30.
**Phases 1 and 2 are signed off on hardware** (a Pico 2 W, 2026-10-30): the board, the
UART0 console and a once-a-second `STAT` line; SD, PWM audio and disc
autoboot; and since Phase 2 the LCD, sent whole from core 1 at about 15 frames
a second. Emulation is at 100% in every mode, MODE 7 included. **Phase 3, the
keyboard, is signed off** (2026-10-31). **Phase 4, sound, is signed off**
(2026-11-01). **Phase 5, speed, is signed off** (2026-11-02). **Phase 6, polish, is signed off**
(2026-11-02), with the brief's end-to-end checklist passed. All phases are done. **Switch the PicoCalc on
whenever the Pico runs**: an unpowered card reads back as zeros until all
power is removed.

**This build has `-fwrapv`, and no other board does yet.** Without it the
emulator hangs after 17.9 minutes, when its cycle counter passes 2^31. Every
other board should have the same fault; see `docs/HANDOFF.md`.

```bash
PLATFORM=picocalc ./build.sh     # HDMI_DRIVER defaults to LCD_SPI; USB_HID=1 is rejected
```

The console runs at 115200 on UART0, and USB-CDC on the Pico's own port is a
second copy.

The `STAT` line reports:
- `emu`: the emulated frame rate
- `busy`: the share of each frame used before the pacer waited
- `lcd`: presents per second
- `push`: frames sent to the panel per second
- `rows`: rows sent to the panel per second; only changed rows are sent
- `lwait`: milliseconds a second core 1 waited for the panel's scan
- `scr`: a hash of BBC RAM &3000–&7FFF
- `disc0`: the disc in drive 0
- `drop`: console characters dropped
- `kbd_err`: keyboard I²C errors and stalled reads
- `snd`: audio samples queued, which hovers near 2,048
- `trim`: audio samples skipped (+) or repeated (−), cumulative
- `und`: audio underruns; each screenshot adds one, as can settings saves
- `sdrop`: audio blocks dropped for lack of room
- `bat`: the raw battery byte, register 0x0B: bits 0–6 percent, bit 7
  charging, so `ac` is 44% and charging. 0% shows as `BAT --`

A test disc at `/micro/disk/drivea.ssd` autoboots with no `micro.ini`. The brief is `REQUIREMENTS.md`, the plan is
`docs/PICOCALC.md`, and decisions are logged in `docs/HANDOFF.md`. Update this
section as phases land.

What makes it different from every other board:

- **The display is an SPI LCD, not a monitor.** A 320×320 ST7365P on `spi1`,
  GPIO 10 to 15. The 320×256 frame is centred, with 32 rows above and below.
  The panel keeps its own frame memory, so scanout is a bandwidth problem
  rather than a real-time one. Core 1 sends only complete frames, and only
  the rows that changed, by DMA at 63 MHz, and the emulator never waits for
  it. The panel's `TE` pin is not wired, but its scan line can be read back
  over MISO, and writes are timed to it. `clk_peri` runs from `clk_sys` to
  allow that clock. The driver is `drivers/lcd_st7365p.c`, built as
  `HDMI_DRIVER=LCD_SPI`. That value is forced on `picocalc` and rejected elsewhere.
- **The keyboard reports characters, not positions.** An STM32 at I²C `0x1F`
  on `i2c1`, GPIO 6 and 7. Its shipped firmware (0x16) **ignores writes to the
  config register**, so Shift always changes the reported code, and the
  release of a key can carry a different code from its press. The layer in
  `frank_kbd_picocalc.c` therefore tracks physical keys. Never sleep or block on
  the bus: the sample driver's 16 ms sleep cannot run on core 0. See
  `docs/PICOCALC.md` section 5.2.
- **A status line in the bottom band**, panel rows 299–308: disc, model,
  speed, panel frame rate, CAPS and SHIFT lights, battery. Core 0 composes it
  in `frank_platform.c` and hands it to the driver with `lcd_status_set()`;
  core 1 sends it between frames.
- **MODE 0 and 3 lose half their horizontal pixels**, as on the PIO video
  path. Blending the pairs was tried and reverted: it read worse, and
  Twisted's 80-column parts already run core 0 at 96–99% `busy`.
- **The arrows, Alt and Ctrl can be a joystick.** The "Joystick" setting,
  Off by default, is the `gamepad` setting saved under INI key `joystick`, so
  that an old card's `gamepad=2` cannot switch it on.
- **No F11 or F12.** Alt+D opens the disc loader, Alt+S the settings, Alt+P
  takes a screenshot. Alt+Esc is SHIFT-BREAK. Never use Alt with the **left**
  Shift: it trips a hidden NumLock in the keyboard firmware that swallows
  Space, `,`, `.` and `B`. The key map is `docs/PICOCALC.md` section 7.
- **Audio is PWM only,** on GPIO 26 and 27, which are one slice. No HDMI audio,
  no I2S. It goes through `drivers/pwm_ring_audio.c`, not `pwm_audio.c`:
  a ring the producer never waits on, which skips or repeats a sample now and
  then to absorb the emulator's 5 ppm rate error.
- **The console is UART0, GPIO 0 and 1,** and it reaches the PicoCalc's own
  USB-C port through a CH340C. It needs its own non-blocking stdio driver,
  because the SDK's `stdio_uart` spins when its FIFO fills.

Hazards:

- **Do not let the PSRAM probe run.** With no PSRAM pin declared,
  `board_config.h` defaults it to GPIO 0, the console's TX pin. Compile the
  probe out on `picocalc`. The board's 8 MB PSRAM (GPIO 2 to 5, 20, 21) is not
  used.
- **Never touch GPIO 23, 24, 25 or 29.** On a Pico 2 W they belong to the
  CYW43 wireless chip, and GPIO 25 is its chip select, not an LED. The
  `PICO_DEFAULT_LED_PIN` block in `main()` must be gated out on `picocalc`. One UF2,
  built with `PICO_BOARD=pico2`, has to run on both modules.
- **GPIO 8 and 9 are a UART link to the STM32.** m1 and m2 use GPIO 8 as their
  PSRAM chip select, so do not copy their headers.
- **The keyboard MCU is also the power manager.** It gates the Pico's power.
  Never write its reset (`0x08`) or power-off (`0x0E`) registers.
- **`clk_peri` caps SPI at 24 MHz** until it is moved to `clk_sys`, which must
  happen before any UART, SPI or I²C init. It is the same trap as in section 6.
