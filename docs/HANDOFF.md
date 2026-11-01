# frank-micro: handoff log

What this file is: a running record of what has been established, what has been
decided, and what has been ruled out, so that none of it gets worked out twice.

It is a log, not a report. Append to it; do not rewrite history in it. If a later
session overturns an earlier entry, add a new entry that says so and leave the
old one in place with a pointer forward. The value of the file is that it shows
what was believed when, and why it changed.

## The four documents and what each is for

| File | Role |
|---|---|
| `CLAUDE.md` | Orientation. Read first. What is live, what is dead, how to build, where to change what. |
| `docs/INVESTIGATION.md` | Evidence. Twenty numbered findings about the repository as it stands, every claim cited to a file and line. |
| `docs/FRUIT-JAM.md` | Plan. What has to change to run on the Adafruit Fruit Jam, phased, with a provenance section recording how strong each claim is. |
| `docs/HANDOFF.md` | This file. Decisions, dead ends, and the register of what still needs hardware. |
| `docs/PICOCALC.md` | Plan for the ClockworkPi PicoCalc (`picocalc`; `pc` before 2026-10-30), added 2026-10-30, in the shape of `FRUIT-JAM.md`. |

None of the four is committed yet. `git status` shows `CLAUDE.md` and `docs/` as
untracked at the time of writing.

## How to add an entry

Each entry gets a date, a one-line statement of what was settled, and where the
detail lives. Absolute dates, never "last week". Say plainly whether something
was read from a file, derived by arithmetic, or assumed, and if it was assumed,
say what would confirm it. If a session tested something on hardware, that is
the most valuable kind of entry there is, so record the board, the build and the
result.

---

## Decisions

### 2026-08-21: the port targets B-em, and `PERFORMANCE.md` is not to be followed

`PERFORMANCE.md` describes the abandoned beebjit architecture. Anything under
`src/micro/`, and 32 of the 34 files in `src/Pico/`, are dead and in several
cases unbuildable. Recorded in `CLAUDE.md` sections 2 and 3, with the evidence in
`docs/INVESTIGATION.md` findings 2 and 3.

### 2026-08-21: new work goes in `src/frank/`, not in `src/bem/`

Local modification of the vendored B-em tree is about 104 lines across six files.
Keeping it that small is what keeps an upstream rebase feasible. `CLAUDE.md`
section 10.

### 2026-08-21: the Fruit Jam port is phased, and phase 1 does not touch the clock tree

Phase 1 boots and displays with a USB-C host adapter, phase 2 adds the codec,
phase 3 deals with the onboard USB-A ports. The reason for the split is that an
HSTX driver and a new board bring-up can each produce a black screen, and a board
with no serial console is a bad place to debug both at once. `docs/FRUIT-JAM.md`
section 8.

### 2026-08-22: phase 3 tests option C before writing any video driver

Changed from the earlier plan, which went straight to HSTX. Option C keeps
252 MHz and the existing PIO DVI driver and runs PIO-USB on the same clock. It
costs a one-line clock change, and if it holds, the HSTX driver and the clock
rework both disappear, and HDMI-embedded audio is kept. The reason this changed
is the finding below about PIO-USB's real clock requirement.
`docs/FRUIT-JAM.md` section 6, option C.

### 2026-08-22: if an HSTX driver is needed, it is vendored, not written

`adafruit/fruitjam-doom`'s `Framebuffer_RP2350.c` supplies the driver and the
scaling; `adafruit/pico-mac`'s `clocking.c` supplies the clock plan and the PLL
search. Both are MIT. Neither is vendored into this repository yet and neither
should be until a phase needs it. `docs/FRUIT-JAM.md` section 6, costs 1 and 2.

**Superseded 2026-08-26. A different driver was vendored, and neither of those
two repositories is on the build machine.** What is on it is
`fhoedemakers/pico_shared`'s `drivers/pico_hdmi`, by way of the vendored copy
inside `fhoedemakers/fruitjam-doom`. It is GPL-3.0, which frank-micro can take
directly, it runs on this board, and it already implements the HDMI data
islands that cost 3 below calls unverified. It is now `drivers/hstx/`. Four
things were changed: 720x576p50 timings, an RGB332 expander configuration in
place of RGB555, the scanline callback and its two line buffers deleted so the
pixel DMA reads framebuffer rows directly, and `DMA_IRQ_0` moved to
`DMA_IRQ_1` because I2S owns `DMA_IRQ_0` in this tree.

### 2026-08-26: HSTX was built anyway, as a quality change rather than a fallback

Phase 3 was contingent on PIO-USB failing at 252 MHz. It did not fail, so the
port shipped on option C. The HSTX driver was written afterwards for the two
picture faults option C carries: the 16-line clip recorded in
`docs/FRUIT-JAM.md` section 3, and the MODE 0 / MODE 3 horizontal loss in
`CLAUDE.md` section 4. Both are properties of the PIO video path. It is a
fourth `HDMI_DRIVER`, not a replacement: `HDMI_PIO_AUDIO` remains the default
on every board including this one.

Three decisions inside it are worth knowing.

**The framebuffer is 720x256 and single-buffered.** Keeping all 640 engine
pixels is what fixes the horizontal loss, and at 8bpp that is 180 KB, so there
is no room for a second buffer. Tearing is handled by phase-locking the
emulator's frame to vsync in `frank_perf_tick()`, with the emulator started
slightly ahead of the beam because the beam is the faster of the two. The lead
is `HSTX_FRAME_LEAD_US`, currently 1500. **This is the part most likely to
need adjusting on hardware.** If it cannot be made to work, the fallback is a
double-buffered 320x256 framebuffer, which is a change to `MICRO_FB_BBC_W` in
`src/board_config.h` plus horizontal doubling in the DMA rather than a
rewrite.

**Rows are 720 bytes wide, not 640.** A video data period is exactly
`MODE_H_ACTIVE_PIXELS` and the DMA reads one byte per pixel, so the pillarbox
border has to be black pixels inside the row. Making the row the full width
was the alternative to splitting each active line into three DMA transfers,
and it keeps the vendored command lists untouched. It costs 20 KB.

**`clk_sys` was not moved.** The first plan followed pico-mac and rebuilt the
whole clock tree around `pll_usb` at 528 MHz. That was unnecessary: 252 MHz is
already proven here with PIO-USB, and the only thing that actually needs a new
clock is `clk_hstx` at 135 MHz. So `pll_sys` is left alone and `pll_usb` is
retasked to 135 MHz instead, with `clk_usb` and `clk_adc` stopped first. The
cost is the native USB controller, which this board does not use.

Everything above compiles; all eleven build variants link. Hardware results
are in the entries below.

### 2026-08-26: retasking pll_usb silently took clk_peri with it

First hardware run: HSTX video came up on the first try, and the board then
stopped at the "SD card could not be mounted" screen with a card in the slot.

`set_sys_clock_khz()` leaves clk_peri attached to **pll_usb**, undivided, at
48 MHz. That is `set_sys_clock_pll()` in the SDK's
`hardware_clocks/clocks.c`, which takes that branch unless
`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK` is set, and nothing here sets
it. So pll_usb is not just the USB PLL: it is the peripheral PLL too, which
the name hides.

Retasking it to 135 MHz for clk_hstx therefore dragged clk_peri from 48 MHz to
135 MHz while `clock_get_hz(clk_peri)` still reported 48. Everything that
derives a divisor from it came out 2.8125 times too fast. SD is on hardware
SPI0 (`drivers/sdcard/sdcard.c` uses `spi_set_baudrate`), and its
`CLK_FAST` of 30 MHz clamped to clk_peri/2, so the data phase ran at 67.5 MHz.
The card answers during the slow initialisation phase and then fails as soon
as the fast clock is selected, which is why the symptom is a mount failure
rather than a card that is not detected. UART1 was 2.8 times off too, so the
console was unreadable at the same time.

The fix is in `hdmi_hstx_clock_init()`: re-point clk_peri at `clk_sys / 3`,
84 MHz, **before** touching pll_usb. That decouples it from the video clock
entirely, so changing the HSTX mode later cannot break SPI again. clk_peri's
divider is 2 bits and integer-only, so 3 is the largest divisor available and
84 MHz is the closest it can get to the SDK's 48.

The function had to move as well. It now runs in `main()` immediately after
`set_sys_clock_khz()` and before `stdio_init_all()`, because a divisor
computed before clk_peri changes is a divisor computed against the wrong
number. It used to run from `graphics_init()`, which is after both the
console and the SD mount.

**Anything else that retasks a PLL on this chip should check clk_peri first.**

### 2026-08-26: single buffering breaks compositing, not just tearing

Second hardware run: the BBC picture was correct, and the F11/F12 overlay
decayed row by row while it was on screen.

The tree composites the overlay once per frame, in `x_gui_end_scanline()`,
after all 256 rows have been blitted. That is correct with two buffers: the
composited buffer is the one presented. With one buffer it is not, and the
reason is the tear control rather than the overlay code.

The emulator writes each row *ahead* of the scanout beam, which is what keeps
the picture from tearing. Working the two timelines through: the beam reads
row r at about 2.4 ms + 64 us x r after vsync, and the next frame's blit
erases row r at about -1.5 ms + 78 us x r. Erase comes before read for every
row on screen. So the overlay was being wiped before the beam ever reached
it, and only restored at the end of the frame, after the beam had passed.
Rows drop out progressively as the two 50 Hz rates drift against each other,
which is exactly what it looked like.

The fix inverts the responsibility: `blit_row()` asks
`frank_ui_row_span()` which horizontal span of the row the overlay owns and
skips it, rather than repainting it and relying on a later composite. The
picture around the window keeps updating. `frank_ui_render()` still runs each
frame and still writes the window, but now into rows nothing else has
touched, and it runs when the beam is near the bottom of the frame, below
almost all of the window.

The general rule for this driver: **on the single-buffered path, anything
drawn on top of the emulator picture has to be written per row, in step with
the blit, not composited after it.** A future overlay that ignores this will
show the same decay.

`frank_ui_row_span()` is `#ifdef HDMI_HSTX`; the PIO boards keep the
composite-after model and their binaries are unchanged.

Two follow-ons came out of the same change, both on hardware.

**A slow tear band crawling up the overlay.** Drawing the window costs about
as long as the beam takes to cross 20 rows, and it was being redrawn every
frame whether or not anything had changed. Once per beat between the
emulator's frame rate and the display's, that redraw crossed the beam. The
band's height and its speed both matched that arithmetic. Fixed by gating
the redraw on a dirty flag, so an unchanged overlay is not rewritten at all;
`frank_ui_handle_key()`, `frank_ui_toggle()`, `frank_ui_open_disk_menu()` and
`frank_ui_toast()` set it. The toast's countdown still ticks every frame,
before the gate.

**Screenshots taken from the F12 menu contained the top half of the menu.**
Not a mid-frame keypress: keys are polled once per emulator frame. The
rasteriser's frame boundary is what does not line up, because
`s_scanline_number` wraps somewhere inside `m6502_exec` rather than at its
edge. So on the frame where the menu closes, the rows emitted before the
keypress had still yielded their span to the overlay and kept it, while the
rows after it were painted clean, and the capture at row 255 got both.
`x_gui_end_scanline()` now holds a pending capture until the overlay is
hidden and two whole rasteriser frames have repainted over it.

The first attempt at that wait was wrong in a way worth recording, because it
looked right and failed intermittently. It armed the countdown from the
overlay's visibility, checked at the frame-complete point. But the keypress
that requests a screenshot from the F12 menu also closes that menu, so by the
time the check ran the overlay already read as hidden while the frame in
progress was still half full of it, and the capture went ahead on the mixed
frame. It appeared to work whenever a leftover toast was still counting down,
because that kept `frank_ui_is_visible()` true and armed the countdown by
accident. The countdown is now armed on the rising edge of the request
itself, which does not depend on what the overlay is doing.

While there, the BMP is now cropped to the 640-pixel picture rather than the
full 720-byte row, so the pillarbox border does not end up in the file.
`MICRO_FB_X_OFFSET` is 0 elsewhere, so the other drivers are unaffected.

### 2026-08-28: the two faults the HSTX build exists to fix are fixed

Confirmed on hardware. MODE 0 and MODE 3 render their 80 columns sharply,
which was the point of carrying 640 engine pixels through to the framebuffer,
and the BBC's full frame reaches the monitor rather than losing its bottom 16
scanlines. Video, the SD card and both overlays all work.

So the trade the earlier revisions of `docs/FRUIT-JAM.md` described as "a
trade, not an upgrade" comes out in favour of HSTX on this board, at the cost
of single buffering and everything that followed from it in the entries
above. `HDMI_PIO_AUDIO` remains the default; `HSTX` is opt-in.

Mounting a disc from the F11 browser and SHIFT-BREAK booting it also works,
which exercises the SD path under load rather than just at mount time.
Screenshots are clean once the capture ordering was fixed.

**MODE 7 is confirmed, so deleting the dedicated teletext renderer was
right.** That renderer exists because the PIO path point-samples 640 engine
pixels into 320, which lands on the SAA5050's interpolated blend columns; it
reconstructs the six real glyph columns from offsets {0,2,5,8,11,14}. At 640
there is nothing to reconstruct, so `blit_row()` ignores the teletext flag
and copies the row. Colour, double height, and contiguous and separated
mosaic graphics all render correctly. The old path is still compiled for the
PIO drivers, not deleted.

The test disc that established this is built by `tools/mkssd.py`, which makes
a DFS image from plain text files meant to be run with `*EXEC`. Disc writes
are off in this build, so building images on the host is the only way to get
a program onto the machine.

The no-SD-card error screen works, static rather than animated. That is
deliberate: it animated by cycling a 64-entry palette under pixels already on
screen, and a palette write repaints nothing on this driver. Redrawing 180 KB
of plasma per frame to animate the screen that says the card is missing was
not worth it. The watchdog reboot on that screen is unchanged and still
wrong; see `docs/INVESTIGATION.md` finding 4.

Still unexercised: audio on either backend, and stability over hours.

### 2026-08-22: under HSTX the framebuffer holds RGB332 bytes, not palette indices

This is the decision that keeps the video path free of a per-line CPU pass. It
moves the palette lookup into `rgb555_to_bbc()`, where a lookup already happens,
instead of adding one to the scanline path that `CLAUDE.md` constraint 7 says not
to add to. `docs/FRUIT-JAM.md` section 6, cost 2.

### 2026-08-22: the Fruit Jam is the only hardware, so the other three boards are frozen

Stated by the user: there is no Murmulator 1, Murmulator 2 or Waveshare Z0 here,
and there will not be. The Fruit Jam is the only board this project can test on.

Three consequences, all of them about verification rather than about what to
build.

**The `m1`, `m2` and `z0` platforms are now permanently unverifiable.** They are
frozen at "believed working, last tested by whoever had the hardware", which for
the tree as it stands is nobody in this project's history. The compile-only sweep
in `CLAUDE.md` section 9 is the only check that remains, and it catches broken
source lists and missing dependencies, not behaviour. So:

- Prefer additive, platform-gated changes to shared code over changes in place.
- Where shared code must change, argue the case for why existing boards are
  unaffected, and write that argument down next to the change.
- `release.sh` can keep building the other seven variants as a compile check, but
  they should not be published as tested.

**The first hardware session has no known-good baseline of our own.** Normally
you flash a build you trust to prove the board, the cable, the monitor and the
card before trusting a new port. That is not available. Use somebody else's
firmware instead: Adafruit ship CircuitPython for this board, and both
`pico-mac` and `fruitjam-doom` publish prebuilt UF2s. Between them they exercise
DVI output, the SD card, the codec and USB host input. Prove the hardware with
those before flashing frank-micro, so that a black screen means our code rather
than a monitor that will not accept a resistor-DVI signal.

CircuitPython is also the cheapest instrument for the small unknowns left in
`docs/FRUIT-JAM.md` section 10: which physical button is GPIO 4 and which is
GPIO 5, the polarity of card detect on GPIO 33, and an I2C scan to confirm the
codec really answers at 0x18.

**SWD matters more than it did.** Release builds have no console at all, and
there is now one board rather than four. The debug connector is confirmed from
the schematic: `DEBUG` is a JST SH 3-pin part with pin 1 `SWCLK`, pin 2 `GND` and
pin 3 `SWDIO`. A probe and that cable are close to essential.

One reassurance from the same source: bricking is not a risk. `BTN0` pulls the
`USBBOOT/BUTTON1` net to ground, and that net is the cathode of `D3` whose anode
reaches `QSPI_CS` through the 1K `R26`. That is the standard BOOTSEL arrangement,
so holding the button while plugging in USB always recovers the board, whatever
the firmware did.

---

### 2026-08-25: the board arrived, and phases 1a and 1b were written

First code for this port. `src/board_fj.h` is new; `CMakeLists.txt`,
`src/board_config.h`, `src/frank/frank_platform.c`, `frank_keyboard.c`,
`frank_settings.c`, `frank_audio.c`, `drivers/audio.c`,
`drivers/ps2/ps2kbd_wrapper.c`, `drivers/usbhid/CMakeLists.txt`,
`drivers/usbhid/tusb_config.h`, `drivers/usbhid/hid_app.c`, `build.sh` and
`README.md` were changed. Nothing has been run on hardware yet.

Every variant compiles and links, with `USB_HID` both off and on. The eight
pre-existing variants are unchanged in behaviour: `m2 HDMI_PIO_AUDIO` with
`USB_HID=0` comes out at the same RAM figure as before the port and 8 bytes
more flash, from the three-way funcsel fix described below.

### 2026-08-25: PS/2 and the NES pad are gated by capability macros, not by pin numbers

`HAS_PS2` and `HAS_NESPAD` are now defined in `board_m1.h`, `board_m2.h` and
`board_z0.h` and deliberately absent from `board_fj.h`, which declares no PS/2
or pad pin numbers at all. Five sites are guarded on them: the `PS2_MOUSE_CLK`
aliasing block in `board_config.h`, the `ps2_init()` call in
`ps2kbd_wrapper.c`, the `ps2kbd_init()` and `frank_gamepad_init()` calls in
`frank_platform.c`, the PS/2 drain in `frank_keyboard.c`, and the body of
`frank_gamepad_init()`.

This is the first working use of the `HAS_*` macros, which until now were
declared in three board headers and read nowhere. Every guard is additive and
the macro is defined for all three existing boards, so those boards come out of
the preprocessor identical to before.

### 2026-08-25: the Fruit Jam console is on UART1, not USB

`CMakeLists.txt` turned UART stdio off unconditionally, and turns USB CDC off
whenever `USB_HID_ENABLED` is set. On the other three boards that coupling is
right, because USB HID owns the native controller. On the Fruit Jam it is not:
the host is on PIO and the native controller is unused.

Rather than run a TinyUSB device stack and a host stack in one binary, UART
stdio is now enabled for `fj` only. The SDK board header puts UART1 on GPIO 8
and 9, both free on the 2x16 header, so a USB-serial adapter there gives a
console in every build. Verified with `arm-none-eabi-nm`: the `fj USB_HID=1`
image contains `stdio_uart` and no `stdio_usb`, where before this change it
would have contained neither and every `printf` would have gone nowhere.

Attaching a CDC device to the native port as well remains possible and is
recorded as a finding below, but was not attempted.

### 2026-08-25: `tools/setup.sh` exists because four things were missing

A build on this machine failed before it started. The `frank-hdmi-sound`
submodule was never initialised, and `CMakeLists.txt` does
`add_subdirectory(frank-hdmi-sound/src)`, so configure died rather than the
link. `PICO_SDK_PATH` was not exported and `build.sh` never set it. `picotool`
and `openocd` were both absent.

`tools/setup.sh` now initialises the submodules, locates the SDK and writes the
export to `tools/env.sh`, which `build.sh` sources, and reports the rest with
Arch package names. It exits non-zero on a blocker, so it works as a gate.

### 2026-08-25: phase 2 done, the codec works, and its levels are driven from the F12 volume

`drivers/tlv320dac3100.c` and `.h` are new. `hardware_i2c` was added to all three
`drivers` link lines and the source to all three source lists; the whole file is
behind `#ifdef CODEC_I2C_ADDR`, so it compiles to four no-op stubs on the boards
that have no codec. `frank_platform.c` calls `tlv320_init(31250)` after the SD
mount and before b-em starts, so a codec that does not answer reports itself on a
screen that already works and cannot stop the machine booting.

**Confirmed on hardware:** sound from the headphone jack with `Audio Out` set to
I2S in F12. This is also the first hardware exercise of I2S on PIO2 and therefore
of the three-way funcsel fix, since a wrong pad function there gives silence.

The clock constants are `P 1, R 2, J 52, NDAC 13, MDAC 2, DOSR 128` from
`tools/tlv320_clocks.py`, putting the PLL at 104.000 MHz and DAC_MOD_CLK at
4.000 MHz at 31,250 Hz. No resampler: the rate is native, so the I2S path in
`frank_audio.c` is unchanged.

Three deliberate departures from pico-mac's sequence, all marked DEPARTURE in the
source:

- **Register 0x33 is not written.** It configures the codec's GPIO1 as an
  output, and on this board GPIO 23 joins that pin to the ESP32-C6's
  `IO9/BOOT9`. Register 0x30, the INT1 routing that goes with it, is also
  skipped. frank-micro has no use for either.
- **NADC and MADC, registers 0x12 and 0x13, are not written.** This part is a
  DAC and has no ADC to divide for.
- **The 1000 ms post-reset sleep is replaced with the datasheet's figures**, 2 ms
  after reset and 10 ms after the PLL powers up.

### 2026-08-25: the F12 volume drives the codec's analogue stage, not a software divide

pico-mac hardcodes the analogue output registers at 50 for the headphones and 40
for the speaker, roughly -26 and -20 dB, and that is what the first working
version used. On hardware it was clearly audible but too quiet even with
frank's software volume at 100 percent, which confirmed the analogue stage was
the whole of the loss.

`tlv320_set_volume()` now maps the 0 to 100 setting onto those registers, with
100 percent at the codec's 0 dB and 0 percent at about -60 dB. `frank_audio.c`
hands the codec full-scale samples and skips the software divide whenever a codec
is present.

Worth understanding rather than just recording: dividing 16-bit samples by up to
100 in software throws away most of the resolution of an already coarse sound
source, and the attenuation then happens *before* a heavily attenuated amplifier.
Doing it in the analogue domain after the DAC costs no resolution at all. The
software divide is kept for boards with no codec, where I2S feeds a bare DAC with
no volume control of its own.

### 2026-10-30: the PicoCalc is a fifth board, `pc`, and is based on `fruit-jam-port`

`REQUIREMENTS.md` (2026-10-07) asks for a ClockworkPi PicoCalc port. It is to be
based on rh1tech `main` if the Fruit Jam work had been merged there, otherwise
on this branch. rh1tech `main` was still `c27ffbf` on 2026-10-30 (read with
`git ls-remote`), so the work branches from `fruit-jam-port` at `d8d25f5`, one
branch per phase: `picocalc-phase0`, `picocalc-phase1`, and so on. The plan is
`docs/PICOCALC.md`. `docs/PICOCALC.md` joins the document table at the top of
this file in the role `docs/FRUIT-JAM.md` has for the Fruit Jam.

### 2026-10-30: PicoCalc commits carry an AI co-author trailer, by the user's choice

`CLAUDE.md` section 10 treats "no AI attribution trailers" as the house rule.
For the PicoCalc branches the user chose the opposite: commits carry a
`Co-Authored-By` trailer, and will be squashed before anything goes upstream.
This overrides section 10 for these branches only. It is not a change to the
rule.

### 2026-10-30: one UF2 for the Pico 2 and the Pico 2 W

The user's test unit is a Pico 2 W, and both modules must work. Decided: build
with `PICO_BOARD=pico2`, and never touch GPIO 23, 24, 25 or 29, whose roles
differ between the two (Pico 2 W datasheet). That means gating the
`PICO_DEFAULT_LED_PIN` block in `main()` out on `pc`, since GPIO 25 is the
CYW43 chip select on a W. No runtime W detection, and no wireless. Every
hardware result records which module it came from. Detail is in
`docs/PICOCALC.md` section 2.2.

### 2026-10-30: PicoCalc key map, agreed in part

Agreed with the user before `docs/PICOCALC.md` was written:

- Symbols type what the PicoCalc legend says. Letters, digits, space, arrows
  and modifiers stay positional.
- F1=f1 … F9=f9, F10=f0.
- Alt+D is the disc loader, Alt+S the settings, Alt+P a screenshot.
  Ctrl+Alt+Del is unchanged.

Proposed but **not yet agreed**: BREAK on Shift+Esc, COPY on Shift+Del, SHIFT
LOCK on Alt+Caps, and the rest of `docs/PICOCALC.md` section 7.2. Phase 0 exits
when the user signs those off. Record the outcome as a new entry.

### 2026-10-30: PicoCalc phase 0 signed off

The user approved the whole of `docs/PICOCALC.md` section 7 and the phased
plan. That includes the parts the earlier key-map entry listed as not yet
agreed: BREAK on Shift+Esc, SHIFT-BREAK on Alt+Shift+Esc, CTRL-BREAK on
Ctrl+Shift+Esc, COPY on Shift+Del, SHIFT LOCK on Alt+Caps, and the symbol
table in section 7.3. Phase 0 has passed its exit check. Phase 1 starts on
branch `picocalc-phase1`.

### 2026-10-30: PicoCalc phase 1 decisions

- **The default model stays Master 128 on `pc`**, the same as every other
  board. This is the user's choice, against the brief's "boots a BBC Model B".
  `micro.ini` and the F12 menu still switch it.
- **The console is UART0 through a new non-blocking stdio driver**,
  `drivers/uart_stdio_nb.c`. `pico_enable_stdio_uart` is off for `pc`. USB-CDC
  is left on, by the existing rule that enables it whenever `USB_HID` is off.
  That gives a second console on the Pico module's own USB port, which costs
  nothing because nothing else uses the native controller on this board.
- **PWM-only audio was moved from phase 4 into phase 1.** With no HDMI audio,
  the existing default for non-HDMI builds is I2S. On `pc`, that would start
  a PIO state machine on the `I2S_*` pins, which board_pc.h defines only so
  `audio.c` compiles. So `FRANK_AUDIO_DEFAULT` is PWM on `pc`, the F12 menu
  offers PWM alone, and both `frank_settings_apply_live()` and
  `frank_audio_set_driver()` force PWM. The PWM push is still the existing
  blocking one; making it non-blocking stays in phase 4.
- **`HDMI_DRIVER=LCD_SPI` is a fifth video build**, and it and `PLATFORM=pc`
  each require the other. `build.sh` defaults to it when `PLATFORM=pc`.
  `USB_HID=1` is rejected for `pc`.
- **A disc for the blind boot needs no INI.** `frank_disk_autoload()` falls
  back to `/micro/disk/drivea.ssd`, so naming the test disc that is enough.

### 2026-10-30: the PicoCalc platform ID is `picocalc`, not `pc`

The user found `pc` confusing. Renamed throughout before phase 1 was pushed:
`PLATFORM=picocalc`, `PLATFORM_PICOCALC`, `BOARD_PICOCALC`,
`src/board_picocalc.h`. `CLAUDE.md`, `docs/PICOCALC.md`, `build.sh` and
`CMakeLists.txt` were updated. The entries above, written before the rename,
say `pc` and are left as written. Read `pc` in them as `picocalc`. The branch
names were already `picocalc-phaseN` and are unchanged.

### 2026-10-30: PicoCalc phase 1 signed off, with MODE 7 below 100%

The user signed off phase 1 on the hardware results recorded under findings:
console, SD mount, PWM audio and disc autoboot all pass. The speed exit was
met in bitmap modes only; MODE 7 runs at 92 to 94%. That shortfall moves to
phase 2, whose LCD row conversion replaces the slow step, and phase 2's exit
should include MODE 7 at 100%. Phase 2 starts on branch `picocalc-phase2`.

### 2026-10-30: PicoCalc phase 2 signed off

The user signed off phase 2 after looking at the panel. The checks:

- MODE 7 and bitmap modes from the demo disc.
- The MOS banner and BASIC prompt, booted from a blank disc.
- Orientation.
- A red, green and blue bar test, which confirmed the colour order.

MODE 7 runs at 100%, which closes the item carried over from phase 1. The
panel receives 14 to 15 frames a second at 24 MHz with blocking SPI. Phase 5
brings changed-row tracking and DMA. Phase 3, the keyboard, starts on branch
`picocalc-phase3`, off `picocalc-phase2`.

### 2026-10-31: PicoCalc phase 3 signed off

The user signed off phase 3 after checking every key, BASIC typing with
symbols, the BREAK variants, Alt+D and Alt+S, and the steady overlay.
`-fwrapv` stays on `picocalc` only, by the user's decision; the other boards
are not to be touched. Phase 4, sound, starts on branch `picocalc-phase4`,
off `picocalc-phase3`, and inherits the `busy` climb recorded below.

## Findings that changed the plan

### 2026-08-25: do not do I2C from the audio producer path

Recorded because it was introduced, shipped, heard, and removed inside one
session, and the reasoning error is easy to repeat.

The first codec version had an automute: count consecutive all-silent buffers,
and mute the output amplifiers after about half a second of silence. Muting the
amps rather than feeding them zeroes is the right way to kill idle hiss, and the
check itself is cheap. The mistake was where it ran.

Unmuting takes three register read-modify-writes, so six I2C transfers, and at
100 kHz that is over a millisecond of blocking. It ran inside
`give_audio_buffer()`, on **every transition from silence to sound**, which on
this machine is every note onset. On hardware the I2S backend was audibly worse
than the HDMI one because of it.

This is the same failure mode as commit `82469e5`, where USB-CDC telemetry
stalling core 0 caused audio dropouts, and it is what `CLAUDE.md` constraint 6
is about. The error in reasoning was checking that the *steady state* was cheap
and not that the *transition* was.

The amps are now unmuted once when the I2S backend is selected and muted once
when it is left, so steady-state I2C traffic is zero and a volume change costs
three writes on a keypress. If idle hiss ever justifies an automute, drive it
from a per-frame tick with a long timeout, never from the producer.

**Confirmed on hardware:** with the I2C removed from the producer path, the I2S
backend was reported as sounding good. So this was the whole of the complaint,
and the I2S ring rework described in the next finding was **not** needed. No idle
hiss was reported either, which is why nothing replaced the automute.

### 2026-08-25: the I2S backend has had none of the dropout work the HDMI path has had

Not a Fruit Jam issue and not new, but it sets expectations for how good I2S can
sound, so it belongs here.

The HDMI audio path has been through four rounds of dropout engineering: a bigger
ring with adaptive fill control (`36649a1`), consumer DC-hold on ring underflow
(`fc1666c`), locking the producer rate to the HDMI audio clock (`f8a4b97`), and
the move to a standard 32,000 Hz sink rate (`ba215c4`).

The I2S path has none of it. `drivers/audio.c` is a **two**-buffer ping-pong of
882 frames each, so:

- `i2s_dma_write()` at `:219-244` **spins with interrupts toggling** until a
  buffer frees, which stalls core 0 and therefore the emulator.
- The two DMA channels chain to each other unconditionally, so if the producer
  is late the DMA **replays the stale contents of the buffer it lands on**. The
  granularity is a whole 882-frame buffer, about 28 ms, which is long enough to
  hear as a stutter rather than a click.

Fixing it properly means a ring of more, smaller slots with the IRQ advancing
through them and filling an unwritten slot with a DC hold. That is a
several-hundred-line change to code shared with three boards that cannot be
tested, so it needs the additive, platform-gated treatment prescribed above.

**It has not been done, and as of 2026-08-25 there is no evidence it is needed.**
The one complaint about I2S quality turned out to be the automute above. Do not
start this speculatively; wait for a symptom that survives the two cheaper
explanations below.

**Two cheaper explanations to rule out first**, if I2S ever sounds worse than
HDMI:

1. **Clipping.** At 100 percent the analogue stage now sits at 0 dB with
   full-scale digital going into it. A few dB of headroom may be all that is
   wanted; try 85 or 90 percent and listen.
2. **Different transducers.** HDMI audio comes out of the monitor's speakers and
   I2S out of the headphone jack or the onboard speaker. Comparing the two is
   not comparing backends, it is comparing loudspeakers.

### 2026-08-25: correction, the SDK on this machine is 2.2.0, not 2.3.0

The table at the end of this file said Pico SDK tag 2.3.0, commit `98a542c1`.
`/home/adq/dev/pico-sdk` is at tag `2.2.0`, commit `a1438df`. That is also what
`CMakeLists.txt:7` pins as `sdkVersion`. Nothing was blocked: the Fruit Jam
board header is present at 2.2.0, which is the release it arrived in.

### 2026-08-25: correction, phase 1a is not silent

`docs/FRUIT-JAM.md:97` describes phase 1a as having no sound, and the effort
table repeats it. That is wrong. The default video driver is `HDMI_PIO_AUDIO`
and `FRANK_AUDIO_DEFAULT` is `FRANK_AUDIO_HDMI`
(`src/frank/frank_settings.h:51-63`), so audio is embedded in the DVI stream and
needs nothing on the board. The codec matters only for the headphone jack and
the onboard speaker.

This has a consequence for phase 2 worth stating plainly: **sound is not a
reason to write the codec driver.** The reason is the 3.5 mm jack and the
onboard speaker.

### 2026-08-25: do not use `tannewt/Pico-PIO-USB`; use upstream at the commit TinyUSB pins

The table at the end of this file records reading `tannewt/Pico-PIO-USB` at
`f3f9d11`, and `docs/FRUIT-JAM.md` builds its PIO analysis on that fork. That
fork cannot be used here, for two independent reasons.

It has **no TinyUSB integration at all**: `PIO_USB_USE_TINYUSB` appears nowhere
in it. And its file layout is flat, with no `src/` directory, no
`pio_usb_host.c`, no `pio_usb_device.c` and no `pio_usb_ll.h`. The Pico SDK's
TinyUSB glue requires all four of those paths, and `hcd_pio_usb.c` includes
`pio_usb_ll.h` directly. It is an old fork that predates the split.

The submodule is therefore `sekigon-gonnoc/Pico-PIO-USB` at `lib/Pico-PIO-USB`,
pinned to `fe9133fc513b82cc3dc62c67cb51f2339cf29ef7`, tagged 0.6.1. That is the
exact commit TinyUSB 0.18.0 lists for it in `tools/get_deps.py:61-63`, which is
the version its `hcd_pio_usb.c` is written against. **Do not float this to the
upstream default branch:** HEAD has since changed the PIO index mapping, so a
newer commit changes behaviour rather than only fixing bugs.

### 2026-08-25: PIO-USB needs one PIO block and three state machines, and it must be PIO0 or PIO1

This is the question phase 1b was told to settle before any code was written.
Read from `lib/Pico-PIO-USB/src/pio_usb_configuration.h` and `src/pio_usb.c` at
the pinned commit.

`pio_usb_configuration_t` carries `pio_tx_num` and `pio_rx_num` as **separate
fields**, which is what raised the worry that host mode needed two blocks. It
does not. The defaults are `PIO_USB_TX_DEFAULT 0` and `PIO_USB_RX_DEFAULT 0`
with `sm_tx` 0, `sm_rx` 1 and `sm_eop` 2, so one block and three state machines,
and `pio_sm_claim` is called exactly three times.

Two constraints come with it. `pio_usb.c:284` and `:287` map the index with
`c->pio_tx_num == 0 ? pio0 : pio1`, so **PIO2 is not addressable** at this
commit. And `pio_usb.c:241` does `pio_add_program_at_offset(pio_usb_tx,
fs_tx_program, 0)`, so the transmit program must sit at instruction offset 0,
which means the block has to be one nothing else has programmed.

So the allocation is forced, and it is the one `docs/FRUIT-JAM.md:986-990`
predicted:

| Block | Claimant | State machines |
|---|---|---|
| PIO0 | video, three TMDS serialisers | 3 |
| PIO1 | PIO-USB | 3, and instruction offset 0 |
| PIO2 | I2S, once the codec driver exists | 1 |

PIO1 is free for this only because the Fruit Jam has no PS/2 socket and no NES
pad connector. On the other three boards PIO1 is exactly full.

Note for later: upstream HEAD, `5a37a66`, replaces the one-bit test with
`pio_get_instance(c->pio_tx_num)` and can reach PIO2. If PIO2 is ever needed for
USB, that is the change to look for, and it comes with a configuration API that
TinyUSB 0.18.0's `hcd_pio_usb.c` does not match.

### 2026-08-25: PIO-USB's other resource claims, and why none of them collide here

- **One DMA channel**, `dma_claim_mask(1 << c->tx_ch)` at `pio_usb.c:325`, with
  a default of channel 0. That default is unusable here, because the video
  driver claims channels dynamically from the bottom. It is pinned to 7 instead,
  the highest channel that collides with neither the hardcoded I2S pair at 10
  and 11 (`drivers/audio.c:47-48`) nor the hardcoded PWM pair at 8 and 9
  (`drivers/pwm_audio/pwm_audio.c:67-68`). `dma_claim_mask` panics on an
  already-claimed channel, so this had to be explicit rather than left to
  initialisation order.
- **Hardware alarm 2**, from `alarm_pool_create(2, 1)` at `pio_usb_host.c:89`,
  driving a 1 ms repeating SOF timer. That is `TIMER0_IRQ_2`, which the
  composite driver also uses; composite is not built on this board, so it is
  free.
- The video driver in the `frank-hdmi-sound` submodule claims its DMA with
  `dma_claim_unused_channel(true)` (`frank_dvi.c:92-93`), so it routes around
  whatever PIO-USB took regardless of ordering.

### 2026-08-25: the SDK's own TinyUSB supplies every bit of PIO-USB build glue

None of the three earlier revisions of `docs/FRUIT-JAM.md` mentions this, and it
removes most of what phase 1b was expected to cost.

`pico-sdk/lib/tinyusb/hw/bsp/rp2040/family.cmake:269-315` defines a
`tinyusb_pico_pio_usb` INTERFACE target that compiles `pio_usb.c`,
`pio_usb_host.c`, `pio_usb_device.c` and `usb_crc.c`, generates the `usb_tx.pio`
and `usb_rx.pio` headers, adds `hcd_pio_usb.c` to `tinyusb_host_base`, defines
`PIO_USB_USE_TINYUSB`, and links `hardware_dma`, `hardware_pio` and
`pico_multicore`. It takes the library path from `PICO_PIO_USB_PATH`, defaulting
to a TinyUSB-internal directory that does not exist in the SDK checkout, because
TinyUSB 0.18.0 declares no submodules and fetches its dependencies with
`tools/get_deps.py` instead.

So the library needs pinning and pointing at, not vendoring. `PICO_PIO_USB_PATH`
has to be set **before `pico_sdk_init()`**, because that is when the SDK
processes its tinyusb directory and `check_and_add_pico_pio_usb_support()` runs.

One more thing worth knowing about the RP2350: `hcd_pio_usb.c:29` guards on
`CFG_TUSB_MCU == OPT_MCU_RP2040`, which looks like it would exclude this chip. It
does not. The SDK builds TinyUSB with `FAMILY rp2040`, and `family.cmake:68`
defines `CFG_TUSB_MCU=OPT_MCU_RP2040` for the whole family, RP2350 included.

### 2026-08-25: with PIO-USB enabled, the native USB controller is left entirely free

`hcd_rp2040.c:30` guards on `!CFG_TUH_RPI_PIO_USB`, so setting that macro
compiles the native host controller out of the build completely. The PIO port
becomes the only host, and `hcd_pio_usb.c:43` sets `RHPORT_OFFSET 1`, so TinyUSB
root port 1 is PIO port 0 and `tuh_init(1)` is the only valid call. The pin,
PIO block and DMA channel reach the driver through
`tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)`, which must
happen before `tuh_init()` because `hcd_init()` is what calls
`pio_usb_host_init()`.

Confirmed on the linked `fj USB_HID=1` image: 32 `pio_usb*` symbols present,
zero native `hw_endpoint_*` or `rp2040_usb_init` symbols.

The consequence is that nothing structural stops a TinyUSB CDC device on the
native USB-C port alongside the PIO host: there is no duplicate `hcd_`
definition to trip over. It needs `CFG_TUD_ENABLED` and `CFG_TUH_ENABLED`
together and both task loops serviced, which is what Adafruit's CircuitPython
does on this board. Not attempted. The UART1 console is the answer instead, and
this is the route back to a USB console if the UART proves inconvenient.

### 2026-08-25: the two-way PIO funcsel is now a three-way test

`drivers/audio.c:99` and `src/frank/frank_audio.c:66-68` both mapped any
non-`pio0` block to `GPIO_FUNC_PIO1`. `docs/FRUIT-JAM.md` called this latent;
moving I2S to PIO2 makes it live, so both are fixed. The `drivers/audio.c` fix
costs 8 bytes of flash on the other three boards and is behaviourally identical
there, since `pio1` still resolves to `GPIO_FUNC_PIO1`.

### 2026-08-22: PIO-USB does not require a 120 or 240 MHz system clock

Two earlier revisions of `docs/FRUIT-JAM.md` stated that it does, and built the
whole clock conflict on it. The assertion came from Pico-PIO-USB's Arduino
example. The library itself contains no clock check: it computes four dividers
from `clk_sys` at run time at `pio_usb.c:1399-1411`, full-speed transmit at
`clk_sys / 48 MHz` and receive at `clk_sys / 96 MHz`. Its C examples say
"Sysclock should be multiple of 12MHz"
(`example/usb_device/usb_device.c:125`), which is what the arithmetic supports.

252 MHz is 21 times 12 MHz and both its full-speed dividers land on exact
multiples of one 256th, the resolution of the PIO's fractional divider field. Its
jitter quantum, one system-clock period, is 4.0 ns against 7.6 ns for the 132 MHz
configuration Adafruit ship and that works.

Status: the arithmetic is verified, the conclusion is not. It needs a hardware
soak. This is now the question that decides the size of phase 3.

### 2026-08-22: HSTX needs no CPU pass and no line buffer for this framebuffer

Also stated wrongly in earlier revisions, which said an index-to-RGB565 pass was
unavoidable. `expand_tmds` is a per-lane bitfield extractor: each lane
right-rotates the shifter by its own amount and takes `NBITS+1` bits from bit 7
down. Read from the field descriptions in
`pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/hstx_ctrl.h`.

`fruitjam-doom/Framebuffer_RP2350.c` then does the rest in shipping code: an
RGB332 lane configuration at `:397-404`, horizontal doubling by one 8-bit DMA
transfer per pixel replicated across the bus at `:300-311` and `:356-364`, and
arbitrary vertical scaling by row-index arithmetic in the DMA command list at
`:362`. Cost to the CPU: nothing.

Consequence beyond the video path: the 16-line clip recorded in
`docs/FRUIT-JAM.md` section 3 is a property of the PIO video path only. Under
HSTX the vertical scale is arbitrary, so it does not arise at any mode.

### 2026-08-22: DMA byte-lane replication is documented, so the pixel doubling is sound

This was the one mechanism in the finding above that rested on another project's
code comment rather than on a specification, so it was checked against the RP2350
datasheet. It is stated three times.

Section 2.1.5, "Narrow IO register writes", page 27: memory-mapped IO registers
"ignore the width of bus read/write accesses" and treat every write as 32 bits,
and "upon a 8-bit or 16-bit write ... the narrow value is replicated multiple
times across the 32-bit data bus, so that it is broadcast to all 8-bit or 16-bit
segments of the destination register". The DMA chapter introduction, section
12.6: "The DMA performs byte lane replication on narrow writes, so byte data is
available in all 4 bytes of the databus, and halfword data in both halfwords."
And the WS2812 PIO example, section 11.6: "each byte the DMA transfers will
appear replicated four times when written to a 32-bit IO register". There is an
official example at `pico-examples/system/narrow_io_write/narrow_io_write.c`.

Three consequences worth keeping:

- Framebuffer alignment does not matter. The read side picks the correct byte
  lane from the transfer size and the address LSBs, so a row can start at any
  byte offset.
- The behaviour can be switched off, and that is a hazard here rather than a
  feature. Setting address bit 14, by addressing the peripheral at +0x4000,
  drives the invalid byte lanes to zero instead. Never use an aliased FIFO
  address in an HSTX driver. The SDK defines no constant for that alias, so it
  cannot be hit by accident.
- One inference remains. The datasheet says "the majority of" IO registers ignore
  access width and does not list them, so that the HSTX FIFO is one of them rests
  on the general rule plus the fact that the Doom port works.

The datasheet was downloaded to a scratch directory for this check and is not
kept in the repository. Re-fetch document RP-008373-DS from
`https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf`. Note
that no PDF tooling is installed on this machine: the text was extracted by
inflating the PDF's content streams with a short Python script.

### 2026-08-22: the codec PLL runs from the bit clock, so MCLK need not be driven

This was the weakest claim in `docs/FRUIT-JAM.md` section 5 and it is now
primary. `pico-mac/src/main.c:640-650` writes register 0x04 to select PLL_CLKIN
as BCLK and CODEC_CLKIN as the PLL, and never touches GPIO 25.

### 2026-08-22: pico-mac's clock plan is better than the one first proposed

Pin `pll_usb` at 528 MHz and feed `clk_sys`, `clk_peri`, `clk_usb` and `clk_adc`
from it with integer dividers, which leaves `pll_sys` free to be reprogrammed for
whatever pixel clock the video mode needs. 528 over 11 is exactly 48 for USB, and
528 over 2 and over 4 give 264 and 132, both exact multiples of 12 MHz.
`clocking.c:190-214` brute-forces `pll_sys` for a requested bit clock. The
earlier plan had this the other way round and pinned the system to 240 MHz, which
would have cost the frame pacer 5% of its margin.

### 2026-08-22: the schematic is public, and it removed phase 1's input path

Adafruit publish the Fruit Jam schematic as Eagle 9.6.2 XML in
`adafruit/Adafruit-Fruit-Jam-PCB`, commit `252dc09`. Netlists come out of
`Adafruit Fruit Jam.sch` with a short Python script over its `<nets>` elements.
That settled every "needs confirmation from the schematic" in
`docs/FRUIT-JAM.md`. Two results matter and four are worth knowing.

**The USB-C port cannot be a host.** `R19` and `R20` put 5.1K from each CC pin to
ground, which is sink termination and not switchable. VBUS on that connector is
an input, reaching the 5 V rail through `Q3`, a P-channel MOSFET gated by the
mechanical power switch. The only firmware-controlled 5 V switch is GPIO 11,
which drives `Q1`, which pulls the gate of `T1`, which feeds the `VBUS` net
through a 1 A polyfuse to the two USB-A sockets and the hub. It never reaches the
USB-C connector. So a USB-C-to-A adapter cannot make that port a host, option A
in section 6 is dead, and PIO-USB is the only input path the board has.

**The DVI wiring is exactly as hoped.** Each of GPIO 12 to 19 reaches the
connector through one resistor of a 220 ohm four-pack, `R14` for 12 to 15 and
`R12` for 16 to 19, and through nothing else. Tracing the resistor gates gives
the polarity too: even GPIOs are the negative halves, odd are the positive, which
matches `board_m2.h:35-42` exactly. The existing pin definitions carry across
unchanged.

Four things nobody had asked about:

- `PERIPH_RST` on GPIO 22 has a 10K pull-up to 3.3 V, so the codec and the
  ESP32-C6 both leave reset with no firmware action. The hazard is asserting that
  line, not forgetting to release it.
- GPIO 23 is a three-way net: the codec's `GPIO1`, the ESP32-C6's `IO9/BOOT9`,
  and the `ESP_BOOT` header. The borrowed codec sequence configures the codec's
  `GPIO1` as an output at register 0x33, which on this board can drive the
  ESP32's boot strap. Decide about that write rather than copying it.
- Card detect on GPIO 33 and both buttons on GPIO 4 and 5 are bare contacts to
  ground with no external resistors. They need internal pull-ups.
- There is no hot-plug detect. The connector's `HOTPLUG` pin goes through a 100K
  over 100K divider to a net that reaches no GPIO, so firmware cannot tell whether
  a monitor is attached.

The hub is a CH334F with its own 12 MHz crystal, no reset or configuration pin
wired, and its 5 V from the switched rail. So it needs GPIO 11 and a settling
delay and nothing else. It has three downstream ports: the two sockets and one on
the 2x16 header at `JP3` pins 14 and 16. Its two front LEDs are driven by the hub
itself.

### 2026-08-22: the codec clock constants are computed, and both Adafruit projects are out of spec

`tools/tlv320_clocks.py` now holds the arithmetic, with every constraint written
next to the clause of TI document SLAS671C it comes from, a solver, an audit mode
and a self-test. Run `python3 tools/tlv320_clocks.py` for the answer and the
working, and `--self-test` after changing it.

The answer for frank-micro, valid at both 31,250 and 32,000 Hz: P 1, R 2, J 52,
NDAC 13, MDAC 2, DOSR 128. 1,348 configurations are valid at both rates; this is
the one the datasheet's own ranking prefers, being the largest NDAC that still
satisfies `MDAC x DOSR / 32 >= RC`.

**This corrects an earlier entry.** The third revision of `docs/FRUIT-JAM.md`
said the two Adafruit projects could not both be right because their sample rates
differ by a factor of 2.2 while their register values are identical. That
reasoning was wrong. Those values encode a ratio, not a frequency:
`NDAC x MDAC x DOSR` is 2048 and so is `32 x R x J / P`, and since the PLL runs
from the bit clock the whole chain scales with the sample rate. The divider chain
is exact at both rates.

What is actually wrong with them is the absolute limits, which do not scale:

- pico-mac at 22,256 Hz puts the PLL at 45.580 MHz, against a specified minimum
  of 80 MHz.
- fruitjam-doom at 49,716 Hz puts DAC_MOD_CLK at 6.364 MHz, against a specified
  maximum of 6.2 MHz.
- The same values at frank-micro's 31,250 Hz would put the PLL at 64 MHz, again
  below the minimum.

Both produce sound regardless, which is worth remembering when judging borrowed
register sequences: working is not the same as correct.

A second correction from the same pass. The third revision said pico-mac's I2S
puts 64 bit clocks in a stereo frame. It does not. `pico-extras`'s
`audio_i2s.pio` is the same program as `drivers/audio_i2s.pio`, and its
`update_pio_frequency()` uses the same `system_clock_frequency * 4 /
sample_freq` divider as `drivers/audio.c:121-123`. All three projects have 32
bit clocks per frame, so BCLK is 32 times the sample rate everywhere.

---

### 2026-08-25: correction, pin Pico-PIO-USB to upstream main, not to the commit TinyUSB names

The entry above says to pin `fe9133f`, tag 0.6.1, because that is the commit
TinyUSB 0.18.0 lists in `tools/get_deps.py`. That was the right instinct for API
compatibility and the wrong one for silicon support, and it cost a hardware
session.

**0.6.1 predates RP2350 support entirely.** It is 58 commits behind upstream
main, and those commits include:

| Commit | What it adds |
|---|---|
| `08a9b06` | workaround for RP2350-E9, which stops PIO-USB host detecting a device at all |
| `a810cb4` | keeps that workaround's code in SRAM |
| `b559b3e` | RP2350 PIO2 support, via `pio_get_instance` and `PIO_IRQ_NUM` |
| `a7d2b61` | pins above 32 on the RP2350B |
| `2f17cec`, `3f80202` | correct `PICO_RP2350` testing, which is always defined and may be 0 |

The E9 one is the one that matters here. The workaround sits in
`pio_usb_bus_get_line_state()` at `src/pio_usb_ll.h:144-160`, and that function
is what `hcd_port_connect_status()` calls. On affected silicon the pad leaks
current while input-enable is held on, so the line-state read is wrong and the
host never sees a device on the port. The fix clears `IE` on both D+ and D-,
waits eight nops for the leak to drain, sets `IE` again, then reads.

It is guarded on `chip_version <= 2`, and the Fruit Jam is A2, so it applies to
this board. Note also its comment that the drain delay was "tested with
264Mhz"; frank-micro runs 252 MHz, so it is inside what upstream has tried.

**Observed on hardware, 2026-08-25.** At `fe9133f` the board booted, displayed
the Master 128 MOS screen correctly and mounted the SD card, and a USB keyboard
in an onboard socket did nothing. Video, SD, PSRAM and the core 1 encoder were
all unaffected, which is what pointed at device detection rather than at the PIO
or DMA allocation.

The submodule is therefore pinned at `5a37a66`. It compiles clean against SDK
2.2.0's TinyUSB 0.18.0 `hcd_pio_usb.c`, so the API worry that motivated pinning
0.6.1 does not arise in practice. **Do not move it back.** If it ever has to
move again, the constraint to check is whether `hcd_pio_usb.c` still builds, not
what `get_deps.py` names.

One consequence worth knowing: at `5a37a66` the PIO index goes through
`pio_get_instance()`, so PIO2 **is** addressable and the one-bit-test constraint
recorded above no longer holds. The allocation is unchanged regardless, because
PIO1 is free on this board and the transmit program still needs instruction
offset 0.

### 2026-08-25: first hardware results, and option C holds

Board: Adafruit Fruit Jam. Build: `PLATFORM=fj`, `HDMI_DRIVER=HDMI_PIO_AUDIO`,
252 MHz, flashed as a UF2 over BOOTSEL. SD card: FAT32 with the `micro` tree
from `sdcard/micro.zip`, no disc images.

**Works, observed directly:**

- Cold boot to the BBC Master 128 MOS screen on a DVI monitor. Display looked
  correct.
- SD card mounted. `SD card mount: OK`.
- PSRAM detected, 7680 kB dlmalloc heap.
- Core 1 HDMI encoder started.
- b-em reached `Using Pico Thumb CPU`.
- USB keyboard in an onboard USB-A socket, behind the CH334F hub, **enumerates
  and types**, at 252 MHz, with the PIO DVI driver running.

**This settles the question that decided the size of the port.** `docs/FRUIT-JAM.md`
section 6 made phase 3, an HSTX video driver plus a clock-tree rework, contingent
on PIO-USB not working at 252 MHz alongside PIO video. It works. Option C holds,
so **phase 3 is not needed**, the PIO DVI driver stays, and HDMI-embedded audio
is kept.

What is proven is enumeration, not stability. The soak is still outstanding, and
the failure mode of a marginal clock is intermittent dropped packets rather than
a clean refusal, so hours of running with the keyboard attached is still the test
that matters.

**Not yet tested on hardware:** MODE 7 versus a bitmap mode, mounting a disc and
SHIFT-BREAK autoboot, HDMI audio, screenshots, `micro.ini` persistence, the F11
and F12 overlays, and the no-SD-card path.

One boot-log observation, pre-existing and not Fruit-Jam-specific:
`src/bem/model.c:46-55` declares `rom_setups[NUM_ROM_SETUP]` with
`NUM_ROM_SETUP` fixed at 5 while `#ifndef PICO_BUILD` removes two of the five
initialisers. So the BBC B model's `swram` lookup fails, the warning then claims
a fallback to `swram` when `rom_setups[0]` is actually `std`, and the search
loop calls `strcmp(NULL, name)` on the two zero-filled entries. It survives only
because there is no MMU and address 0 is readable on this part. Harmless in
practice, identical on all four boards, and out of scope while the rule about
keeping `src/bem/` divergence small stands.

### 2026-10-30: the PicoCalc keyboard's config register cannot be written

Read from `clockworkpi/PicoCalc` at `fdcfe2c`, keyboard firmware
`BIOSVERSION 0x16`. In `receiveEvent()` in `picocalc_keyboard.ino` there is no
case for `REG_ID_CFG` (0x02), so a write falls into `default` and is discarded.
`reg_init()` sets `CFG_USE_MODS | CFG_REPORT_MODS`. So the brief's plan to clear
`CFG_USE_MODS` at boot, to get raw key positions, cannot work on that firmware.
Shift always changes the reported code, F6–F10, BREAK, HOME, END, PgUp, PgDn and
Insert exist only as Shift layers, and the release of a key can carry a
different code from its press. The key layer is designed around this. See
`docs/PICOCALC.md` section 5.2. Whether the user's unit runs 0x16 needs
hardware.

### 2026-10-30: on `pc` the PSRAM probe defaults to the console pin

Read: with no `PSRAM_CS_PIN_RP2350A` in the board header, `board_config.h`
defaults it to 0. `main()` calls `psram_init(get_psram_pin())`
unconditionally, and on an RP2350A that pin is GPIO 0, the PicoCalc's UART0 TX
(schematic net `UART0_TX`). The probe has to be compiled out for `pc`.

### 2026-10-30: PicoCalc schematic results

Read from the mainboard v2.0 schematic (text extraction). Every pin in the
brief is confirmed. Further facts:

- GPIO 8 and 9 are `UART1`, wired to the STM32's `UART3`. They are not in the
  brief.
- PSRAM uses GPIO 2 to 5, 20 and 21, as a quad interface.
- GPIO 28 goes to an expansion header.
- The LCD connector's `TE` pin has no net, so there is no tearing signal.
  That is weaker than a positive read; confirm visually.
- The USB-C port goes through WAS7227 switches to either the STM32's USB or a
  CH340C, whose serial side is switched between Pico UART0 and STM32 UART1.
  Derived: `SW701` selects, and the PicoCalc's own USB-C can be the frank-micro
  console.
- The STM32 owns `PA_EN`, `HP_DET`, the backlights and `PICO_EN`.

### 2026-10-30: `stdio_uart` blocks core 0 when its FIFO fills

Read from the SDK's behaviour: `uart_putc()` spins on a full 32-byte TX FIFO,
87 µs a character at 115,200 baud. Unlike USB-CDC, there is no timeout macro.
The PicoCalc gets a ring-buffered, interrupt-driven stdio driver that drops when
full (`docs/PICOCALC.md` section 5.3). The Fruit Jam's UART1 console has the
same latent cost. It is not changed, and is recorded here only.

### 2026-10-30: build environment for the PicoCalc work

On this machine (Ubuntu 26.04), installed with apt: `gcc-arm-none-eabi`
14.2.1, `libnewlib-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, cmake
4.2.3, and ruby with erb, which Ubuntu's package includes. `tools/setup.sh` then
cloned Pico SDK 2.2.0 to `~/dev/pico-sdk` with tinyusb, and initialised the
repository submodules. The SDK's `boards/pico2.h` was then read: default UART0
on GPIO 0/1, LED on 25. That settles the assumption `docs/PICOCALC.md` section
12 recorded.

### 2026-10-30: PicoCalc phase 1 builds, and the other boards are byte-identical

`PLATFORM=pc` builds clean: 338,220 bytes of flash and 277,792 bytes of RAM
(53.0% of 512 KB). That is well inside N5's ceiling of 393,000. `SCRATCH_X`
and `SCRATCH_Y` are 2 KB each. The ELF has `uart_stdio_nb_init` and
`stdio_usb_init`, and no `stdio_uart`.

Frozen boards: all 22 existing variants (m2, m1 and z0 in each driver, fj in
`HDMI_PIO_AUDIO`, `HDMI_PIO` and `HSTX`, each with `USB_HID` 0 and 1) were
built out-of-tree before and after the change, from the same directory.
**All 22 `.bin` files are byte-identical** (SHA-256), so the change cannot
alter the behaviour of any existing board, which is a stronger result than the
preprocessor argument. The configure-time checks were also tested: `pc` with
`HDMI_PIO_AUDIO`, `m2` with `LCD_SPI`, and `pc` with `USB_HID=1` each stop with
their own message.

Not yet run on hardware.

### 2026-10-30: PicoCalc phase 1 on hardware, and two speeds that are not 100%

Run on the test unit, a Pico 2 W in a PicoCalc, flashed and inspected over SWD
with a Raspberry Pi Debug Probe. The firmware was unchanged from the build entry
above.

What passed:

- The console arrives on the PicoCalc's USB-C port through the CH340C, at
  115200, with `drop=0` throughout. `SW701` was left as the unit came, and that
  position works.
- The SD card mounts. The card is the PicoCalc's own, 8 GB SDHC, FAT.
- With no `micro.ini`, the board boots the Master 128 and the startup beep
  comes out of the speakers, so PWM audio on GPIO 26 and 27 works.
- `/micro/disk/drivea.ssd`, here `bs-twisted.ssd` from `src/bem/pico/discs/`,
  autoboots and `scr` changes every second while the demo runs.

What did not:

- **MODE 7 runs at 92 to 94%.** The Master's start-up screen and the demo's
  opening both give 46 to 47 fps with no idle time. A heavier section of the demo
  gave 83 to 89%. Its later sections run at 100% with 9 to 41% idle.
- Sampled over SWD, core 0 is busy in `draw_bytes_teletext` (B-em's rasteriser,
  in RAM) and `blit_row_teletext`, `lum5` and `rgb555_to_bbc` (`frank_gui.c`, in
  flash). `clk_sys` was read back as 252 MHz and the flash as 84 MHz, divider 3,
  so the board is not running slow.
- It is not the blocking PWM push. Swapping in the non-blocking push on
  `picocalc` changed nothing, and was reverted.
- That code is shared, so the other boards very likely run MODE 7 at the same
  speed. Nobody has measured them.
- Phase 2 replaces the row conversion for the LCD, which is where to win this
  back. **Phase 1's "100% speed" exit holds in bitmap modes only.**

Do not repeat these traps:

- **The PicoCalc must be switched on whenever the Pico runs.** With the
  PicoCalc off and the Pico powered from its own USB, the card is fed through
  the SPI pull-ups. It still initialises and answers commands, and reports its
  size, but every sector reads as zeros with a valid CRC. The mount fails with
  `FR_NO_FILESYSTEM`.
- The card stays in that state after the PicoCalc is switched on, until all
  power is removed. A Pico reset or CMD0 does not clear it. An afternoon went
  into debugging the SD driver before this was found. The driver is fine.
- **Sampling the PC over openocd's telnet port at a fixed cadence aliases with
  the 50 Hz frame.** It reported 92% of the time in the pacer's
  `busy_wait_until` while the core was in fact saturated. Backtraces taken
  through gdb at irregular intervals gave the true picture.

Files can be put on the card with no card reader: stop at the line after
`f_mount()` in `main()`, `restore` the file into `screen_mem` from gdb, and call
`f_mkdir`, `f_open`, `f_write` and `f_close` on the target. That is how
`drivea.ssd` got there, and a read-back compared equal.

### 2026-10-30: PicoCalc phase 2, the panel driven and MODE 7 at 100%

**The LCD driver.** `drivers/lcd_st7365p.c` now drives the panel:

- **Init sequence.** It is ClockworkPi's `pico_lcd_init()` at `fdcfe2c`, byte
  for byte, with one change: `COLMOD` is 0x55 (RGB565) rather than 0x66, so a
  pixel is two bytes.
- **Core 1 owns the panel.** The init's 460 ms of sleeps run there, so core 0
  boots without waiting. Core 1 then sends every row of the newest presented
  frame with blocking SPI at 24 MHz.
- **Push rate: 14–15 frames a second.** The estimate was 18; the gap is per-row
  conversion and call overhead. DMA and changed-row tracking are Phase 5.
- **Core 0 is unaffected.** `graphics_set_buffer()` is still a pointer store.

**MODE 7 speed.** Forty gdb backtraces, taken at irregular intervals on the
MODE 7 screen, split core 0's time like this:

- **About half:** frank's teletext blit, `blit_row_teletext()` with `lum5()`
  and `rgb555_to_bbc()`, running from flash.
- **About 40%:** B-em's teletext rasteriser.
- **The rest:** almost none of it was the 6502.

The LCD build now has its own blit in `src/frank/frank_gui.c`, under
`VIDEO_LCD_SPI`, built on three changes:

- **A 32 KB lookup table.** It maps RGB555 to `lum5 << 3 | bbc_index`. The
  key squeezes out the unused bit 5, which an exhaustive host check confirmed
  is lossless.
- **A fast path for uniform cells.** A cell whose six samples are the same
  pixel is written as background.
- **Word stores from RAM** to clear the work rows. `memset()` was 12% of the
  frame, from flash.

The scanline hooks also run from RAM on this build. The bitmap blit uses the
same table.

**The output is byte-identical.** Framebuffers dumped over SWD on the BASIC
screen, from the old and new blits, contain exactly the same two images:
cursor on and cursor off.

Measured, `STAT` at 1-second intervals:

| Screen | Before | After |
|---|---|---|
| Master BASIC prompt, MODE 7 | 90–91%, no idle | 100%, busy 69% |
| `bs-twisted` opening, MODE 7 | 92% | 100%, busy 74% |
| `bs-twisted` heaviest raster section | 83–89% | 100%, busy 78–87% |
| `bs-twisted` palette-effect section | 98–101%, pinned | 100%, busy 65–70% |

What remains on the busiest sections is B-em's own work:
`invalidate_tiles_for_palette_index()`, and `__popcountsi2` from libgcc,
which runs from flash. B-em stays untouched.

**RAM** is 313,212 bytes (59.7%), up 35 KB from Phase 1, almost all of it the
table. N5's ceiling is 393,000.

**Frozen boards:** the out-of-tree sweep was rerun after the change. All 22
existing variants build, and every `.bin` is byte-identical to the Phase 1
baseline.

**Not yet done:** nobody has looked at the panel. Colour and orientation are
unchecked. `LCD_SWAP_RB` flips red and blue if needed.

### 2026-10-30: `spi_write_blocking()` returns before its last byte has left

The first Phase 2 build showed uniform noise on the panel, even though
`STAT` said frames were being sent.

**What SWD readback showed.** The panel's status registers had taken
everything: `RDDPM` 0x9C, `MADCTL` 0x48, `COLMOD` 0x55. But `RAMRD` returned
noise everywhere, even outside the BBC frame, where the clear should have left
black.

**What isolated it.** Pixel writes issued by hand from gdb read back correctly
at every clock from 2 to 24 MHz. So the panel and the wiring were fine, and
the firmware's own writes were going wrong.

**The cause.**

- The SDK's `spi_write_blocking()` checks the PL022 busy flag immediately
  after its final FIFO write.
- From RAM at 252 MHz, core 1 gets there before the flag has risen, so the
  call returns while the byte is still shifting out.
- The window-and-`RAMWR` code was inlined into the RAM-resident core 1 loop,
  so DC went high while `RAMWR` was still on the wire.
- The panel samples DC on the last bit, so it took `RAMWR` as data. It never
  entered pixel-write mode, and every pixel was discarded.
- The init's register writes survived only because `lcd_cmd_data()` ran from
  flash, slowly enough to hide the race. ClockworkPi's sample also writes from
  flash.

**The fix.** `lcd_send_sync()` uses `spi_write_read_blocking()`, which cannot
return until every byte has been clocked back in. Anything followed by a DC or
CS change goes through it: commands, parameters, and the last row before CS
rises. Bulk rows still use `spi_write_blocking()`, since no pin changes between
them.

**After the fix,** `RAMRD` showed black outside the frame and demo colours
inside. The push rate and emulation speed were unchanged. **That was not
enough on its own**: the panel still showed mostly noise. See the next entry.

**The rule for any later SPI code here, Phase 5's DMA included:** never move
DC or CS on the strength of `spi_write_blocking()` returning.

**Debugging notes:**

- This FatFs build has no `f_rename` or `f_unlink`. To boot to BASIC,
  `drivea.ssd` was overwritten with a 512-byte blank catalogue (boot option 0,
  800 sectors), then restored from `src/bem/pico/discs/bs-twisted.ssd`.
- Once, a `reset run` straight after a gdb session that had called into the
  firmware left core 0 spinning at boot on the SDK's timer spin lock (lock 10),
  with core 1 still in the bootrom. Three further resets booted cleanly. Treat
  it as an artefact of the debug session unless it recurs from a power-on.

### 2026-10-30: the panel drops commands when CS and DC move at full speed

With `lcd_send_sync()` in place, the panel still showed mostly its random
power-on memory, with one narrow band of BBC picture.

**What isolated it.**

- A firmware test wrote a known full-screen pattern through the normal
  `lcd_window()` path at 24 MHz. Read back from gdb, it sometimes landed in
  full and sometimes not at all, with identical code.
- Everything gdb sent itself always landed: commands, register reads, and a
  single 160 KB data stream at 24 MHz. The difference is that gdb takes
  milliseconds between moving CS or DC and starting the next transfer.
- The firmware's own `RAMRD` readback returned zeros every time. Its
  `CASET`, `RASET` and `RAMRD` were sent at full speed, then read at 1 MHz.
  So the commands were the part being lost, not the pixel data.

**The cause.** Commands were lost at random when CS or DC changed within
nanoseconds of a transfer starting or ending. When `CASET`, `RASET` or `RAMWR`
is missed, a whole frame goes nowhere. The power-on clear could also be lost,
which is why the noise filled the whole panel. The panel's write clock is
rated to 15 MHz, so at 24 MHz there is no margin in any of its SPI timings.

**The fix.** `lcd_cs()` and `lcd_dc()` wait 64 cycles, about 250 ns, on each
side of the pin change (`LCD_SETTLE_CYCLES`). That is a few microseconds a
frame. With it, the test pattern landed on every run, and a firmware
self-check confirmed it in steady state. The self-check read one column back
from the panel every 30 frames and compared it with what had been sent: zero
mismatches in the picture area.

**Readback traps, for whoever debugs this panel next.**

- `RAMRD` returns one dummy byte, then 2 bytes per pixel in `COLMOD` 0x55.
- Reading while the firmware is pushing frames tells you nothing unless the
  commands before the read land reliably. All-zero reads are not proof that
  the panel's memory is blank.
- gdb sessions that call into the firmware can leave core 0 hung in
  `advance_hardware()`. Reset before trusting `STAT` again.
- The old gdb scripts used `screen_mem[1]` as scratch, which scribbles on a
  live framebuffer. Harmless for a frame, but misleading in a dump.

### 2026-10-30: the PicoCalc keyboard, as found on the unit

Probed with a temporary build before Phase 3 was planned.

- **The event FIFO (register 0x09) behaves exactly as the firmware source at
  `fdcfe2c` says.**
  - Letters arrive lower case.
  - Shift+3 arrives, and is released, as `#`.
  - Modifiers send their own events.
  - Held printable keys, Enter and arrows re-report as *pressed* every
    100 ms. Held modifiers and other keys report *hold*.
- **No gap is needed** between the write of 0x09 and the read.
  ClockworkPi's sample sleeps 16 ms; that is unnecessary.
- **At 10 kHz one FIFO transaction takes about 5 ms.** The driver queues
  three a frame, all run by the I²C block's own command FIFO.
- **The FIFO is full of stale events at boot.** They are discarded until the
  first empty read.
- **The version register reads `00 00`,** not the source's 0x16.
- **Alt plus left Shift switches on a hidden NumLock.** The firmware then
  swallows Space `,` `.` `B` until Shift is next pressed with another key.
  The user moved SHIFT-BREAK from Alt+Shift+Esc to Alt+Esc because of it.

### 2026-10-30: every board hangs after 17.9 minutes; fixed on the PicoCalc only

**Symptom.** The PicoCalc's screen froze and the console went silent. Core 0
was spinning in `advance_hardware()` (`src/bem/thumb_cpu/src/cpu_mem.c`).
Both times it happened, `lcd_present_count` read exactly 53,714. That
deterministic count is what gave it away; the first occurrence had been
wrongly blamed on gdb calls into the firmware.

**Cause.**

- 53,714 frames of 40,000 cycles is just over 2^31. The hardware event queue
  keeps `int32_t` cycle timestamps and compares them as `(a - b) <= 0` (in
  `upsert_hw_event()` and `advance_hardware()`), which is meant to be safe
  across the wrap.
- But signed overflow is undefined in C, and at `-O2` GCC compiles it as a
  direct `cmp a, b`.
- Once the counter passes 2^31, every pending event looks overdue, and the
  loop never returns.

**Fix.** `-fwrapv` on the `frank-micro` target, for `picocalc` only.
`advance_hardware()` then compiles to a subtract and a compare with zero.

**Other boards.** Every other board compiles the same code with the same
flags, so they should all hang at 17.9 minutes too. On a board with the
watchdog running and no debugger attached, it would show as a reboot. The
fix is a one-line CMake change, but those boards are frozen until someone can
run them past 18 minutes, so it has not been applied to them. The user
decided on 2026-10-31 to leave them alone in this work. `CLAUDE.md`
section 9's "several minutes of running" check is too short to catch this:
run for at least 20.

### 2026-10-30: the overlay flickered on the PicoCalc LCD

Core 1 takes about three emulated frames to send each buffer to the panel, so
core 0 is usually repainting the buffer being sent. Two fixes, both from the
HSTX build, which has the same hazard for a different reason:

- **`blit_row()` leaves alone the span the overlay owns**
  (`frank_ui_row_span()`, now built for `VIDEO_LCD_SPI` too). Otherwise the
  overlay was missing from rows repainted mid-send. This alone reduced the
  flicker.
- **The overlay is redrawn only when it changes.** It is drawn twice per
  change, once into each buffer. A row sent between the background fill and
  the text had flickered.

The user confirmed both pop-ups steady.

### 2026-10-31: on the PicoCalc, `busy` climbs to 100% while the CPU is idle

**What was seen.** On a static screen with no input, `busy` rose steadily
from about 74% to 100% over about seven minutes, then stayed there. It
reproduced from a reset: Esc at 9 s, then hands off. Emulation stayed at
100% throughout.

**What it is not.** Backtraces taken at the 100% reading put about 40% of
core 0 in the pacer's idle wait, so the CPU was not saturated. But a gdb halt
pauses the timer while the PWM audio DMA keeps draining, so halting disturbs
exactly the thing being measured. `busy` read 74% straight after the samples.

**Cause.** The producer and the output run at slightly different rates, and
the output's wait was counted as work.

- The PWM side plays exactly 31,250 Hz: wrap 8063 at 252 MHz, read back,
  from the same crystal as the timer.
- The emulator makes one sample per 64 emulated cycles. `m6502_exec()` sets
  each frame's limit relative to where the last frame stopped
  (`get_cpu_timestamp() + 40000`, `src/bem/thumb_cpu/src/adapter.c`), so
  each frame's overshoot is never paid back and the emulator runs slightly
  fast.
- Measured with phase 4's ring, from the slope of its fill over nine
  minutes: the producer is **about 5 ppm fast**, 0.16 samples a second.
- `pwm_audio_push_samples_blocking()` starts with both its buffers full
  (the pre-roll), so from boot every push can wait. As the producer drifts
  ahead, the wait inside the push grows by microseconds a second, and the
  pacer's idle wait shrinks to match. `busy` counts the first and not the
  second, so it climbs until audio is pacing the emulator outright.

Five ppm is the right direction but on the small side to account for the
whole 26-point climb in seven minutes. The drift may depend on what the
6502 is running (instruction length sets the overshoot), and the original
measurement was on an idle BASIC prompt. Not pursued further, because the
fix below does not depend on the exact figure.

**Fixed in phase 4, on the PicoCalc only.**

- `drivers/pwm_ring_audio.c` replaces the blocking push with a 4,096-sample
  ring that the producer never waits on.
- The ring keeps about 2,048 samples queued by skipping or repeating at most
  one sample per 882-sample block. That handles 35 samples a second, against
  the 0.16 measured.
- Over ten minutes `busy` stayed flat at 76–77%. There were no underruns
  and no dropped blocks.

**Not fixed at the source.** Paying back the overshoot in `adapter.c` would
cure the producer itself. But that is vendored `src/bem/` code shared by
every board, and the other boards are frozen. They keep the blocking I2S and
PWM pushes, so their pacer figures will show the same slow climb. It is
harmless there apart from what the pacer reports, and HDMI audio there
resamples into a ring of its own.

### 2026-11-01: switching sound off looped the last 131 ms on the PicoCalc

Sound off sets B-em's `sound_internal` false, and `sound_poll_n()` then
produces nothing at all, rather than producing silence. The ring's DMA never
stops, so with no pushes it replayed the ring until sound came back on. Each
push now writes silence over everything between what it just queued and
the output's read position. A producer that stops for any reason, whether
sound off or a long stall, now runs out into silence. That costs about
2,000 stores a push.

### 2026-11-01: MODE 7 text vanished on coloured backgrounds

Found with `tools/mkmode7test.py`'s test disc. Yellow on red, white on blue
and double height on blue showed as bare coloured bars, and yellow on blue
was garbled.

**Cause.** `blit_row_teletext()` calls the darkest of a cell's six samples
the background, and paints only samples brighter than it. "Brightness" is
`lum5()`, the brightest channel, so every primary is 31 and foreground and
background tie. Raw source rows, captured over SWD, show the samples are
clean: background at 31 per channel, foreground at 30, and blends of 10 and
20 only between them.

**Fix, PicoCalc only.** Snap each sample to its nearest BBC colour on its own.
A per-channel snap of a blend between two primaries always lands on one of
the two, so no background has to be identified. The margin pixels take
glyph column 0, which is blank in every SAA5050 alphanumeric. The right
margin takes column 5 when columns 3 to 5 agree, so contiguous mosaics
still join. Text on black renders as before.

**Every PIO build has the same fault.** That is m1, m2, z0, and fj without
HSTX. Their `blit_row_teletext()` is the original algorithm. They are frozen
by the user's decision, so it is recorded here, not fixed. HSTX is not affected:
it copies all 640 pixels and has no teletext renderer.

**Not a fault, as far as can be told.** On the test disc, a normal row after
a lone double-height top half shows blank. Normal-height characters on the
bottom row of a double-height pair also show blank. Both come from the
emulator's own rasteriser, which is shared with every board, so they are
B-em's model of the SAA5050. They have not been checked against a real BBC.

### 2026-10-31: the frozen-boards sweep must normalise the build date

The SDK's binary info embeds the build date (`__DATE__`, e.g. `Oct 31 2026`)
in every firmware image. A sweep run on a different day from its baseline
therefore differs on all 22 variants with no source change. Building the
Phase 2 commit, which matched on the 30th, reproduced the new hashes on the
31st. Before hashing, replace the date string in the `.bin` with the
baseline's (the same length). With that, Phase 3 matched the baseline on all
22.

## Dead ends, so nobody retries them

### The PicoCalc keyboard's raw-matrix registers, 0x0C and 0x0D

The firmware source has "C64" registers returning the raw key matrix (nine
bytes) and the arrow keys. Reading physical key state would have removed every
code-mapping problem in `docs/PICOCALC.md` section 5.2. On the unit they
answer with their headers, but never showed a key down across five held keys,
so Phase 3 uses the event FIFO.

### `drivers/HDMI_vga_hstx.c` is not a starting point for an HSTX driver

It includes `disphstx.h` from the DispHSTX library, which is not vendored
anywhere in this repository, so it cannot compile rather than merely being
uncompiled. It targets analogue VGA for a 360x240 Amstrad CPC framebuffer and
carries frank-cpc headers. `docs/INVESTIGATION.md` finding 11.

### `HAS_HSTX` and the claim at `src/board_m2.h:12-13` mean nothing

`HAS_HSTX` is defined and used nowhere. Nothing in frank-micro or in the
`frank-hdmi-sound` submodule touches the HSTX peripheral. The comment claiming
HSTX HDMI is the default video and audio path is inherited from frank-cpc and is
false.

### Option A, a USB-C host adapter for input, is not possible

The USB-C port is wired as a device: 5.1K on both CC pins to ground, VBUS an
input through a P-channel MOSFET gated by the mechanical power switch, and no
firmware-controlled way to source 5 V on that connector. A conducting P-FET is
bidirectional, so a board powered from the 2x16 header with the switch closed
would back-feed roughly 5 V onto the USB-C VBUS, and a keyboard does not care
about CC. Do not build on that: it is off-specification, depends on a switch
position, and back-powers a connector designed as an input.

### `tools/micro_console.py` cannot work

It is frank-cpc's script and speaks the line-based protocol from the dead
`src/Pico/micro_serial_console.c`. The live console takes single bytes.
`docs/INVESTIGATION.md` finding 10.

### Do not take `gtf.c` from pico-mac

It computes VESA GTF timings, which are not the CEA-861 timings a television
expects. The `frank-hdmi-sound` submodule already carries a hand-tabulated table
including `dvi_timing_720x576p_50hz`.

### Do not take fruitjam-doom's clocking

It derives `clk_hstx` from `clk_sys` on a fractional divider and lands on
125.875 MHz instead of 126, which its own comment calls nonsense
(`Framebuffer_RP2350.c:201-205`). Use pico-mac's PLL search instead.

### Do not trust the codec PLL constants in either Adafruit project

`pico-mac/src/main.c` and `fruitjam-doom/src/i_main.c:59-190` contain the same
register block: P 1, R 2, J 32, NDAC 8, MDAC 2, and DOSR left at its reset 128.
It is a valid ratio at any sample rate, which is why it appears twice, but it
puts the PLL below its specified minimum at pico-mac's rate and DAC_MOD_CLK above
its specified maximum at Doom's. See the finding above, and use
`tools/tlv320_clocks.py` rather than copying either. The sequence around the
constants, the reset, the routing and the mute handling, is still worth taking.

### `csr.N_SHIFTS` alone cannot pixel-double

The data-repeat behaviour in the field description repeats the whole shifter
word, so 8bpp gives p0 p1 p2 p3 p0 p1 p2 p3. It doubles pixels only in
combination with a one-pixel-per-word DMA transfer.

### `CSR.COUPLED_MODE` is not a shortcut

It connects eight PIO output bits into HSTX bit selects 24 to 31, which would let
a PIO do a palette lookup feeding the hardware TMDS encoder. It requires
`clk_hstx` to be driven directly from `clk_sys`, which throws away the clock
decoupling that is the whole reason for using HSTX.

---

## Needs hardware

Nothing in any of these four documents has been tested on a Fruit Jam. No Fruit
Jam code has been written. The full list of open questions is
`docs/FRUIT-JAM.md` section 10; these are the ones that block a decision rather
than a detail.

| Question | Blocks | Cheapest way to settle it |
|---|---|---|
| **Answered 2026-08-25: yes.** Does the `fj` build boot to the MOS screen on a DVI monitor? | — | Done. See the first-hardware-results entry |
| **Answered 2026-08-25: yes, it enumerates.** Does PIO-USB work at 252 MHz with a keyboard behind the hub? | Settled the size of the port: phase 3 is not needed | Done, once the library was moved off 0.6.1 |
| Does that keyboard stay up over hours? | Nothing structural, but a marginal clock shows as intermittent dropped packets | Leave it running with the keyboard attached and type into it periodically |
| Do MODE 7 and a bitmap mode both render correctly? | Nothing. They take different blit paths in `frank_gui.c` | `MODE 7` then `MODE 1` from BASIC |
| Does a disc mount and SHIFT-BREAK autoboot? | Nothing | Put an `.ssd` in `/micro/disk/` and use F11 |
| **Answered 2026-08-25: yes.** Does the codec PLL lock at the computed constants? | — | Done; sound from the headphone jack |
| Does the onboard speaker work? | Nothing. Same driver, different output | Attach a speaker to the JST connector |
| Is the analogue stage clipping at 100 percent volume? | Nothing now: I2S was reported sounding good once the automute was removed. Left here only as the first thing to check if distortion is ever reported | Listen at 85 percent and compare |
| Which physical button is GPIO 4 and which is GPIO 5? | Nothing. Cosmetic | The board layout or a multimeter. The schematic's net and part names are crossed |

Settled since the last revision of this table, both by reading source rather
than by testing: how many PIO blocks and state machines PIO-USB needs, and which
blocks it can address. See the 2026-08-25 findings.

Resolved since the first version of this table: whether the USB-C port can be a
host (no), and whether the HSTX pins are directly driven (yes). Both from the
schematic, both recorded above.

### PicoCalc, added 2026-10-30

Phase 1 has been run on the test unit, which has a **Pico 2 W**; see the
hardware entry above.

| Question | Blocks | Cheapest way to settle it |
|---|---|---|
| What SPI clock does the ST7365P take before the picture corrupts? | N2, the display frame rate | Phase 5: step 24 → 25.2 → 31.5 → 42 → 63 MHz |
| Does the keyboard STM32 need a gap between the FIFO write and the read? | Keyboard throughput | Phase 3: queue back-to-back first |
| Does the unit's keyboard firmware report version 0x16? | Whether the config-write finding applies | Phase 3 boot log |
| Is the LCD `TE` pin really unconnected? | Tear-free display | Visual check of the schematic |
| Does the CYW43 stay off with `WL_ON` undriven? | Nothing functional | Current draw, W against non-W |
| Does any of it run on a non-W Pico 2? | The both-modules requirement | Someone with a non-W runs the checklist |

## External sources consulted, with commits

Re-clone at these commits rather than at a branch tip. None is vendored into this
repository and none should be until a phase needs it.

| Repository | Commit | What it was read for |
|---|---|---|
| `adafruit/pico-mac` | `59c910b` | HSTX driver structure, the 528 MHz clock plan and PLL search, TLV320DAC3100 bring-up |
| `adafruit/fruitjam-doom` | `f2ded0b` | RGB332 and 4bpp lane values, DMA pixel doubling, command-list vertical scaling, a second copy of the codec sequence |
| `tannewt/Pico-PIO-USB` | `f3f9d11` | The divider computation, the absence of any clock check, the `pio0`-or-`pio1` restriction. **Not usable in the build**, see the 2026-08-25 finding: no TinyUSB integration and the wrong file layout |
| `sekigon-gonnoc/Pico-PIO-USB` | `5a37a66`, vendored as the submodule `lib/Pico-PIO-USB` | The PIO block and state-machine count, the DMA and alarm claims, the instruction-offset-0 constraint, and the RP2350-E9 line-state workaround. Pinned here rather than at the 0.6.1 that TinyUSB names, because 0.6.1 predates RP2350 support; see the 2026-08-25 correction |
| Pico SDK | tag `2.2.0`, commit `a1438df`, at `/home/adq/dev/pico-sdk` | Fruit Jam pin numbers, HSTX register semantics, clock aux sources, PLL VCO range, and the TinyUSB PIO-USB build glue in `lib/tinyusb/hw/bsp/rp2040/family.cmake`. Earlier revisions of this table said tag 2.3.0, commit `98a542c1`; that was wrong |
| RP2350 datasheet | document RP-008373-DS, fetched 2026-08-22 | Narrow IO register writes at section 2.1.5, DMA byte-lane replication at section 12.6 |
| TLV320DAC3100 datasheet | document SLAS671C, fetched 2026-08-22 | Every constraint in `tools/tlv320_clocks.py`, cited there by clause |
| `adafruit/pico-extras` | default branch, fetched 2026-08-22 | `audio_i2s.c` and `audio_i2s.pio`, to establish that BCLK is 32 times the sample rate in the Adafruit projects too |
| `adafruit/Adafruit-Fruit-Jam-PCB` | commit `252dc09` | The schematic, as Eagle XML. Cited by net name and part designator |

`PICO_SDK_PATH` was not set in the environment while these documents were
written. SDK paths are relative to `/home/adq/dev/pico-sdk`, found by searching.

PicoCalc, added 2026-10-30:

| Repository or document | Commit | What it was read for |
|---|---|---|
| `clockworkpi/PicoCalc` | `fdcfe2c` | Keyboard firmware (`Code/picocalc_keyboard/`), the LCD sample (`Code/picocalc_helloworld/lcdspi/`), the mainboard v2.0 schematic, the ST7365P datasheet. Fetched, not cloned |
| Pico 2 W datasheet | fetched 2026-10-30 | Roles of GPIO 23, 24, 25 and 29 |
