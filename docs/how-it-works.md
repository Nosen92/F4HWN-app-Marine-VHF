# How Marine VHF works

## The VFO underneath

An overlay app has no API to tune the radio from scratch: the loader sets the BK4829 up
for the current VFO before `app_main()` (front-end filter, IF bandwidth, squelch
thresholds, PA calibration), and `tx_prepare()` keys that VFO's TX settings. Marine VHF
therefore needs the VFO to be a plain marine channel:

- RX and TX frequency in 156.000-162.050 MHz: the VHF front end (below 280 MHz), the
  squelch thresholds and PA calibration of the 137-174 MHz band, and above all
  `tx_state()`, which checks the radio's frequency lock (F Lock) on the VFO's TX frequency,
  so the app's TX is allowed exactly when the radio would allow TX on a marine frequency;
- FM, wide (25 kHz channels, 16K0G3E), no CTCSS/DCS, no compander.

When it is not (checked with `beam_get`, `rx_freq`, `tx_freq`), the app calls
`handoff("Marine VHF")` and `vfo_stage()` with channel 16 (156.800 MHz, FM, wide, no
codes, 25 kHz step; power and TX lock kept) and returns. The loader saves the VFO as a
keypad entry would, then starts the app again. This is the pattern Scan 406 uses to hand a
channel to EPIRB 406. If the VFO is still not marine on the second start, the app shows
"Set the VFO to 156.800 FM wide" instead of trying again. The VFO stays on 156.800 after
the app exits.

## Tuning

A channel is tuned by writing REG_38/39 (10 Hz units) and cycling REG_30 through 0 to
restart the synthesizer (VCO recalibration), as the in-tree scanners do: 0xBFF1 for RX
(the value of `BK4819_RX_TurnOn`). The speaker amplifier is switched off first, since the
REG_30 cycle steps the AF output (a click when amplified).

The receiver listens on the coast frequency of a duplex channel and on the ship frequency
otherwise; it always transmits on the ship frequency (docs/channel-plan.md).

Audio follows the squelch (REG_0C bit 1), with the radio's own SQL level: speaker and green
LED on while it is open, or while SIDE1 is held (monitor).

## Dual watch

Following ETSI EN 301 025-1 clause 5.4.3 (the clause that fixed marine radios with dual
watch are tested against):

- channel 16 is sampled every `DW_PERIOD_MS` = 1.6 s (at least every 2 s);
- a look: tune to 16, wait `DW_SETTLE_MS` = 50 ms, then up to `DW_READS` = 6 squelch reads
  10 ms apart; two open reads in a row count as a signal. A look keeps the receiver off the
  working channel for about 110 ms (the standard allows 150 ms), and sampling goes on while
  the working channel is receiving, as the standard asks;
- a signal on 16 holds the receiver there: two beeps, the backlight on, "16" blinking in
  the main area (inverse and plain alternately), "16 BUSY  PTT nn" on the bottom line,
  until the squelch has stayed closed on 16 for `DW_HANG_MS` = 2 s;
- PTT always transmits on the working channel (EN 301 025-1: the transmitter is on the
  selected channel). To answer on 16, press MENU first: 16 becomes the working channel.
- Dual watch is off on 16 itself, while monitor is held, and when the radio's squelch is
  set to 0 (REG_78 reads 0): with the squelch always open, 16 would always look busy.

In the emulator (tools/screenshots.py): 16 sampled every 1601 ms, 110 ms per look, a
signal starting on 16 at 3.000 s caught at 3.268 s, back on the working channel 2 s after
it ended.

The settle time and the number of reads are the values to adjust on the air if the
squelch turns out slower to open than this after a retune.

## Transmit

On PTT, unless the channel is receive-only (2xxx) or `tx_state()` refuses (frequency
lock, battery):

1. `tx_power()`: LOW4 (about 0.5 W, inside the 0.1-1 W that EN 301 178 asks of the low
   setting) on LO and on 75/76, HIGH (about 5 W) on HI;
2. `tx_prepare()`: the VFO's TX setup without the PA (PA_ENABLE off);
3. REG_36 (PA bias) set to 0, the ship frequency written, REG_30 cycled to 0xC1FE (mic
   ADC, TX DSP, PA gain, as `BK4819_TxOn_Beep`), 20 ms for the PLL to lock, REG_36
   restored;
4. `tx_carrier(true)`: PA on.

On release, `tx_end()` (PA off, the VFO's RX registers) and the working channel is tuned
again. A transmission is cut after 5 minutes (stuck PTT); PTT must then be released.

The PTT is read on its pin (PB10, as the firmware's `GPIO_IsPttPressed()`), not only
through `get_key()`: the firmware gives the keypad priority over the PTT, so a key pressed
during a transmission would otherwise look like a PTT release (TX off, the key acted on,
TX on again). Other keys are ignored while transmitting.

## Display

The status line holds TX / RX / MON, HI or LO, DUP or RX ONLY, and the firmware's battery
gauge. The channel designator is drawn in big digits, 44 pixels high (Roboto Condensed
Bold, 16 to 26 pixels wide), two digits with the leading zero, centred and proportionally
spaced. A four-digit channel is drawn as the IC-M94 draws it: the last two digits exactly
where the two-digit channel is (the emulator checks that the 19 of 1019 and 2019 is the 19
of channel 19 pixel for pixel), and the 10 or 20 in 22-pixel digits in front, 4 pixels
away, on the same baseline. Channel 16 is drawn inverse (EN 301 178 wants it "distinctively
marked"). While a channel is typed, the digits so far and a blinking underscore where the
next one goes; from the third digit on, the first two move to the small prefix. The bottom line shows "DUAL 16", what dual watch is
doing, or a message for 1.5 s.

The font is not in the code: `gen_assets.py` packs `digits.txt` and `digits_small.txt`
(text art, made from the TTF by `tools/render_digits.py` and editable by hand) into the
app's read-only assets, which the loader keeps in the slot's header sector, outside the
4 KiB overlay. The assets start with the 20 widths (big 0-9, small 0-9), then each big
digit as 6 LCD pages and each small one as 3, in column bytes, in the frame buffer's own
layout: `glyph()` reads one digit with `asset_read()` into a 192-byte stack buffer and
XORs it into frame-buffer lines 0-5 (big) or 3-5 (small). The frame buffer is blank under a
glyph, or solid in the dark box of an inverse 16, so the same XOR draws both. Blinking (16 held by dual watch, the entry cursor) redraws on each change of
a 512 ms phase.

## Keys

Holding UP/DOWN steps once, then again every 150 ms after 1 s (`REPEAT_DELAY_MS`,
`REPEAT_MS`). Each step tunes the channel at once, which also postpones the next look at
16, so dual watch does not interrupt the scrolling.

Channel entry works like a TV remote (`chEntryComplete`): after each digit the app looks
for a longer designator that begins with the digits typed so far. If there is none, the
entry is complete and the channel is taken at once: 3, 4, 5 and 9 (no channel 3x, 4x, 5x,
9x), any two digits except 10 and 20, any four. Otherwise it waits for a pause of
`ENTRY_MS` = 1 s: 1 then a pause is 01, 10 then a pause is 10 (or go on: 1019). The test
program `test/host_channels.c` checks this for every channel and for the cases above.

## UV-K1 and UV-K5 V3 keys

The channel keys go through the firmware's `nav_dir()`, which turns the raw KEY_UP /
KEY_DOWN into +1/-1 according to the radio's navigation setting (SetNav): UP/DOWN on the
UV-K5 V3, RIGHT/LEFT on the UV-K1. Every other key has the same code on both radios.

## Memory

The 4 KiB overlay holds code, constant data and .bss: 3548 B of code and tables and 80 B
of .bss, 3628 B in all (89 %; the receive-only build is 3200 B). The font is 1826 B of
assets (the asset area takes up to 3840 B). The channel table is 58 16-bit designators; frequencies are
computed. There is no division (the Cortex-M0+ has no divide instruction and libgcc's would
cost space): digits are taken off by subtraction, numbers in text go through the
firmware's `append_u32`.

Saved settings (4 bytes, `cfg_save` after every change): working channel, the channel MENU
goes back to, HI/LO, dual watch.

## What is tested, and how

- `tools/check_channels.py` builds the channel code for the PC and compares every entry
  with `tools/app18_reference.csv`, Appendix 18 typed in row by row: receive and transmit
  frequencies, receive-only and 1 W flags, which rows are in and which are out, the order.
  Both option sets. It caught a units slip in the 01-28 formula during development.
- `tools/screenshots.py` runs the ARM binary in the firmware's own overlay emulator
  (`App/apps/app_screenshot.py`: Unicorn, on top of the built Fusion ELF, with the
  firmware's real drawing code) with a simulated squelch, saves the screens in docs/img,
  and checks the radio sequences: dual-watch timing, the TX key-up order, a key pressed
  under PTT, the keys (`nav_dir`, digit entry, MENU), the power rules and the VFO staging.

Not tested yet: anything on a real radio. Squelch timing after a retune, the PLL lock time
in TX, deviation and power on the marine channels, and the UV-K5 V3 are unverified.
