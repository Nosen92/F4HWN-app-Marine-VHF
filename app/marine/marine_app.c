/* Marine VHF: an IC-M94-style marine VHF handheld for the F4HWN Fusion overlay
 * apps (Quansheng UV-K1 / UV-K5 V3).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Channels: the international plan of ITU Radio Regulations Appendix 18
 * (Rev.WRC-19), and nothing national or regional (no USA/Canada "A"/"B"
 * channels, no Nordic L1-L3, no UK M1/M2, no private channels, no weather
 * channels). docs/channel-plan.md gives the sources and every choice made.
 *
 * Radio: the app runs on top of the current VFO and needs it to be a plain
 * marine channel (156.000-162.050 MHz, FM, wide, no CTCSS/DCS, no compander).
 * When it is not, the app stages the VFO on channel 16 and starts itself again
 * (vfo_stage + handoff): the loader then sets up the VHF front end, the 25 kHz
 * filters and the PA calibration for the marine band, and the firmware's own
 * TX checks (frequency lock, battery) look at a marine frequency. Each channel
 * is then tuned by writing REG_38/39 and restarting the synthesizer (REG_30).
 *
 * Dual watch (ETSI EN 301 025-1 5.4.3): channel 16 is sampled every
 * DW_PERIOD_MS, each look away from the working channel lasting well under
 * 150 ms. A signal on 16 holds the receiver there (two beeps, "16" blinks)
 * until DW_HANG_MS after it ends. PTT always transmits on the working channel.
 *
 * Keys: UP/DOWN (UV-K1: LEFT/RIGHT, through nav_dir) channel -/+, repeating
 *       after 1 s held · 0-9 channel entry, TV-remote style · MENU 16, again:
 *       back · * HI/LO ·
 *       F dual watch on/off · SIDE1 (hold) monitor · PTT · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifndef HOST_TEST
#include "../app_api.h"
#endif

#define APP_TITLE   "Marine VHF"   /* APP_NAME in build.sh: the self hand-off */

/* ---- build options ---- */
#ifndef MARINE_TX
#define MARINE_TX         1   /* 0: receive-only build, PTT refused            */
#endif
#ifndef MARINE_VDES_VOICE
#define MARINE_VDES_VOICE 0   /* 1: also 24-26 and 84-86 as duplex voice       */
#endif
#ifndef MARINE_LOW_15_17
#define MARINE_LOW_15_17  0   /* 1: also hold 15 and 17 at low power           */
#endif

/* ---- channel plan (frequencies in 10 Hz units) ---- */
#define F_LO     15600000u    /* 156.000 MHz: the App18 band                    */
#define F_HI     16205000u    /* 162.050 MHz                                    */
#define F_16     15680000u
#define F_DUPLEX   460000u    /* coast leg = ship leg + 4.6 MHz                 */

/* The selector, in order: numeric, as on the IC-M94, each four-digit channel
 * right after its two-digit one. 1xxx: the ship (lower) leg used as a
 * single-frequency channel; 2xxx: the coast (upper) leg, which a ship only
 * receives (App18 note mm). 27 and 28 are split since 1 Jan 2019 (note z):
 * 1027/1028 carry voice, 2027/2028 are ASM 1/2 (data). 70 is DSC only. */
static const uint16_t CH[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 1019, 2019, 20, 1020, 2020, 21, 22, 23,
#if MARINE_VDES_VOICE
    24, 25, 26,
#endif
    1027, 1028,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76, 77,
    78, 1078, 2078, 79, 1079, 2079, 80, 81, 82, 83,
#if MARINE_VDES_VOICE
    84, 85, 86,
#endif
    87, 88,
};
#define NCH ((uint8_t)(sizeof CH / sizeof CH[0]))

/* The two-digit channel of a designator. */
static uint8_t chBase(uint16_t d){ while (d >= 1000u) d -= 1000u; return (uint8_t)d; }

/* Ship (lower-leg) frequency of a two-digit channel: 01..28 from 156.050 MHz,
 * 60..88 from 156.025 MHz, 50 kHz apart (25 kHz interleaved overall). */
static uint32_t chShip(uint8_t n){ return (n < 60u ? 15600000u : 15302500u) + (uint32_t)n * 5000u; }

/* Two-frequency channels of App18 (a coast-station frequency is listed). */
static bool chDuplex(uint8_t n){
    return n <= 5u || n == 7u || (n >= 18u && n <= 28u) ||
           (n >= 60u && n <= 66u) || (n >= 78u && n <= 86u);
}

static uint32_t chRx(uint16_t d){
    const uint8_t n = chBase(d);
    uint32_t f = chShip(n);
    if (d >= 2000u || (d < 1000u && chDuplex(n))) f += F_DUPLEX;
    return f;
}
static uint32_t chTx(uint16_t d){ return chShip(chBase(d)); }   /* always the ship leg */
static bool chRxOnly(uint16_t d){ return d >= 2000u; }
/* 75/76: 1 W at most (note n). 15/17: on-board use at 1 W ERP (note g). */
static bool chLowOnly(uint16_t d){
    return d == 75u || d == 76u
#if MARINE_LOW_15_17
        || d == 15u || d == 17u
#endif
        ;
}
static uint8_t chFind(uint16_t d){ uint8_t i = 0; while (i < NCH && CH[i] != d) i++; return i; }

static const uint16_t P10[4] = { 1000u, 100u, 10u, 1u };

/* Keypad entry, as with a TV remote: v is the number made of the n digits
 * typed so far. It is complete when no longer designator begins with those
 * digits (3 can only be 03, 72 only 72): the channel is then taken at once.
 * Otherwise (1: 01, 10-19, 1019...) it is taken when the typing pauses. */
static bool chEntryComplete(uint16_t v, uint8_t n){
    if (n >= 4u) return true;
    for (uint8_t i = 0; i < NCH; i++){
        const uint16_t d = CH[i];
        const uint8_t more = (uint8_t)((d < 100u ? 2u : 4u) - n);   /* digits after v */
        if (more == 0u || more > 3u) continue;                      /* not longer     */
        const uint16_t scale = P10[3u - more];
        const uint16_t lo = (uint16_t)(v * scale);
        if (d >= lo && d < lo + scale) return false;
    }
    return true;
}

#ifndef HOST_TEST
/* ---- timing (ms) ---- */
#define LOOP_MS        10u
#define DW_PERIOD_MS 1600u    /* 16 sampled at least every 2 s                  */
#define DW_SETTLE_MS   50u    /* synthesizer and squelch settle on 16, then ... */
#define DW_READS        6u    /* ... up to 6 squelch reads 10 ms apart: a look  */
                              /* lasts about 110 ms (EN 301 025-1: 150 ms max)  */
#define DW_HANG_MS   2000u    /* held on 16 this long after its signal ends     */
#define TX_LOCK_MS     20u    /* PLL lock on the TX frequency before the PA     */
#define TOT_MS     300000u    /* stuck-PTT guard: 5 minutes                     */
#define MSG_MS       1500u
#define ENTRY_MS     1000u    /* a pause in the typing takes the channel        */
#define REPEAT_DELAY_MS 1000u /* UP/DOWN held this long: steps repeat ...       */
#define REPEAT_MS     150u    /* ... every 150 ms                               */

/* ---- BK4829 ---- */
#define REG_0C 0x0Cu          /* bit 1: squelch open                            */
#define REG_30 0x30u
#define REG_36 0x36u          /* PA bias and gain                               */
#define REG_38 0x38u
#define REG_39 0x39u
#define REG_78 0x78u          /* squelch RSSI thresholds: 0 when squelch is off */
#define R30_RX 0xBFF1u        /* as BK4819_RX_TurnOn                            */
#define R30_TX 0xC1FEu        /* as BK4819_TxOn_Beep: mic ADC, TX DSP, PA gain  */

/* PTT, read on its pin as GPIO_IsPttPressed() does (PB10, low when pressed):
 * get_key() gives the keypad priority, so a key pressed during a transmission
 * would hide the PTT. No API service reads it alone. */
#define GPIOB_IDR (*(volatile const uint32_t *)0x50000410u)
#define PTT_PIN   (1u << 10)

/* tx_power levels (OUTPUT_POWER_*). LOW4 is about 0.5 W: inside the 0.1-1 W of
 * EN 301 178 even with spread between radios. HIGH is about 5 W (6 W max). */
#define PWR_LO 4u
#define PWR_HI 7u

/* ---- channel digits: a bitmap font in the assets (gen_assets.py) ----
 * 10 big widths, 10 small widths, then each big digit as 6 LCD pages (ink on
 * rows 2-45: frame-buffer lines 0-5) and each small one as 3 pages (ink on
 * rows 24-45: lines 3-5, the same baseline), in column bytes, ready to OR in.
 * The small ones are the 10 or 20 in front of a four-digit channel. */
#define BIG_PAGES   6u
#define SMALL_PAGES 3u
#define SMALL_LINE  3u        /* the first frame-buffer line of a small digit   */
#define GLYPH_MAXW 32u
#define DIG_GAP     3u        /* between big digits                             */
#define SMALL_GAP   2u        /* between small digits                           */
#define PREFIX_GAP  4u        /* between the small prefix and the big digits    */
#define CURSOR_Y0  41u        /* the entry cursor: an underscore at the baseline */
#define CURSOR_Y1  45u

typedef struct { uint8_t magic, ch, prev, flags; } cfg_t;
#define CFG_MAGIC 0xA7u
#define CFG_HI    1u
#define CFG_DW    2u

static struct {
    const app_api_t *A;
    uint8_t  ch, prev;        /* working channel; the one MENU goes back to     */
    uint8_t  key;             /* key held at the last poll                      */
    uint8_t  entry;           /* digits typed (channel entry), 0: none          */
    uint16_t entryV;          /* the number they make                           */
    uint8_t  phase;           /* blink phase (512 ms) of the last frame drawn   */
    bool     hi, dw;          /* settings                                       */
    bool     on16;            /* dual watch holds the receiver on 16            */
    bool     tx, pttHeld;     /* transmitting; PTT still held after a refusal   */
    bool     open, spk, mon;  /* squelch, speaker amplifier, monitor            */
    bool     sqlOff;          /* radio squelch level 0: dual watch cannot work  */
    bool     running, dirty;
    const char *msg;          /* bottom-line message, MSG_MS long               */
    uint32_t tDw, tHang, tTx, tMsg, tEntry, tDraw, tKey, tRepeat;
    /* last: the byte fields above stay within the 0-31 offsets of a Thumb ldrb */
    uint8_t  gw[20];          /* widths of the big, then small digits (assets)  */
} g;
#define A (g.A)

/* ---- small helpers ---- */
static uint8_t slen(const char *s){ uint8_t n = 0; while (s[n]) n++; return n; }
static char *put(char *o, const char *s){ while (*s) *o++ = *s++; return o; }
static char *putCh(char *o, uint16_t d){ if (d < 10u) *o++ = '0'; return A->append_u32(o, d); }
static void message(const char *s){ g.msg = s; g.tMsg = A->ticks_ms(); g.dirty = true; }

static void save(void){
    const cfg_t c = { CFG_MAGIC, g.ch, g.prev,
                      (uint8_t)((g.hi ? CFG_HI : 0u) | (g.dw ? CFG_DW : 0u)) };
    A->cfg_save((const uint8_t *)&c, sizeof c);
}

/* ---- radio ---- */
static bool squelchOpen(void){ return (A->bk_read(REG_0C) & 2u) != 0; }

static void speaker(bool on){
    if (on == g.spk) return;
    g.spk = on;
    if (on) A->set_af(APP_AF_FM);
    A->audio_path(on);
    A->led(on);
}

/* Frequency, then a REG_30 cycle: VCO recalibration and relock. */
static void tune(uint32_t f, uint16_t r30){
    A->bk_write(REG_38, (uint16_t)f);
    A->bk_write(REG_39, (uint16_t)(f >> 16));
    A->bk_write(REG_30, 0);
    A->bk_write(REG_30, r30);
}
/* The amplifier goes off first: the REG_30 cycle steps the AF output. */
static void tuneRx(uint32_t f){ speaker(false); tune(f, R30_RX); }
static void tuneWork(void){ tuneRx(chRx(CH[g.ch])); g.tDw = A->ticks_ms(); }

static void beep2(void){
    for (uint8_t n = 2; n; n--){ A->play_tone(2000u, 60u); A->delay_ms(40u); }
    A->tones_off_rx();                 /* RX link back; play_tone left the speaker off */
    g.spk = false;
    A->led(false);
}

static void selectCh(uint8_t i){
    g.ch = i;
    g.on16 = false;
    g.entry = 0;
    if (CH[i] == 16u) g.hi = true;     /* selecting 16 sets full power (EN 301 178) */
    tuneWork();
    save();
    g.dirty = true;
}

/* EN 301 025-1 5.4.3: 16 is sampled at least every 2 s; a look away from the
 * working channel lasts at most 150 ms; a signal on 16 is followed for as
 * long as it lasts. */
static void dualWatch(uint32_t now){
    if (!g.dw || CH[g.ch] == 16u){
        if (g.on16){ g.on16 = false; tuneWork(); g.dirty = true; }
        return;
    }
    if (g.on16){
        if (g.open) g.tHang = now;
        else if (now - g.tHang >= DW_HANG_MS){ g.on16 = false; tuneWork(); g.dirty = true; }
        return;
    }
    if (g.mon || g.sqlOff || now - g.tDw < DW_PERIOD_MS) return;
    g.tDw = now;
    tuneRx(F_16);
    A->delay_ms(DW_SETTLE_MS);
    uint8_t hits = 0;
    for (uint8_t k = DW_READS; k && hits < 2u; k--){   /* two reads in a row */
        hits = squelchOpen() ? (uint8_t)(hits + 1u) : 0u;
        if (hits < 2u) A->delay_ms(10u);
    }
    if (hits < 2u){ tune(chRx(CH[g.ch]), R30_RX); return; }
    g.on16 = true;
    g.tHang = now;
    A->backlight_on();
    beep2();
    g.dirty = true;
}

#if MARINE_TX
static const char *txRefusal(void){
    if (chRxOnly(CH[g.ch])) return "RECEIVE ONLY";
    switch (A->tx_state()){
        case 0:  return NULL;
        case 2:  return "BATTERY LOW";
        case 3:  return "VOLTAGE HIGH";
        default: return "TX LOCKED";      /* the radio's frequency lock (F Lock) */
    }
}

static void txOn(void){
    const uint16_t d = CH[g.ch];
    speaker(false);
    g.on16 = false;
    A->tx_power(chLowOnly(d) || !g.hi ? PWR_LO : PWR_HI);
    A->tx_prepare();                   /* the VFO's TX setup, PA_ENABLE left off */
    const uint16_t pa = A->bk_read(REG_36);
    A->bk_write(REG_36, 0);            /* no PA drive while the synthesizer moves */
    tune(chTx(d), R30_TX);             /* the channel's ship frequency           */
    A->delay_ms(TX_LOCK_MS);
    A->bk_write(REG_36, pa);
    A->tx_carrier(true);
    g.tx = true;
    g.tTx = A->ticks_ms();
    g.dirty = true;
}

static void txOff(void){
    A->tx_end();                       /* PA off, the VFO's RX registers */
    g.tx = false;
    g.spk = false;
    tuneWork();
    g.dirty = true;
}
#endif

static void ptt(bool down, uint32_t now){
    if (!down){
#if MARINE_TX
        if (g.tx) txOff();
#endif
        g.pttHeld = false;
        return;
    }
    if (g.pttHeld) return;
    g.entry = 0;                       /* PTT drops a channel being typed */
#if MARINE_TX
    if (g.tx){
        if (now - g.tTx >= TOT_MS){ txOff(); message("TX TIMEOUT"); g.pttHeld = true; }
        return;
    }
    const char *why = txRefusal();
    if (!why){ txOn(); return; }
    message(why);
#else
    (void)now;
    message("RECEIVE ONLY");
#endif
    g.pttHeld = true;
}

/* ---- keys ---- */
static void step(int8_t dir){
    uint8_t i = g.ch;
    if (dir > 0) i = (uint8_t)(i + 1u < NCH ? i + 1u : 0u);
    else         i = (uint8_t)(i ? i - 1u : NCH - 1u);
    selectCh(i);
}

/* The channel typed, or a message when there is none. */
static void entryDone(void){
    const uint8_t i = chFind(g.entryV);
    g.entry = 0;
    g.dirty = true;
    if (i < NCH) selectCh(i); else message("NO SUCH CHANNEL");
}

static void onKey(uint8_t key, uint32_t now){
    const int8_t dir = A->nav_dir(key);
    g.dirty = true;
    if (dir){ step(dir); return; }
    if (key <= APP_KEY_9){
        if (!g.entry) g.entryV = 0;
        g.entryV = (uint16_t)(g.entryV * 10u + key);
        g.entry++;
        g.tEntry = now;
        if (chEntryComplete(g.entryV, g.entry)) entryDone();
        return;
    }
    g.entry = 0;
    switch (key){
        case APP_KEY_MENU:                 /* the 16/C key */
            if (CH[g.ch] != 16u){ g.prev = g.ch; selectCh(chFind(16u)); }
            else selectCh(g.prev);
            break;
        case APP_KEY_STAR:
            g.hi = !g.hi;
            save();
            break;
        case APP_KEY_F:
            g.dw = !g.dw;
            save();
            break;
        case APP_KEY_EXIT:
            g.running = false;
            break;
        default:
            break;
    }
}

/* ---- display ---- */
static void fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on){
    for (uint8_t x = x0; x <= x1; x++)
        for (uint8_t y = y0; y <= y1; y++){
            uint8_t *p = &A->fb[y >> 3][x];
            const uint8_t m = (uint8_t)(1u << (y & 7u));
            *p = on ? (uint8_t)(*p | m) : (uint8_t)(*p & ~m);
        }
}

/* One digit at column x, big or small. XORed in: the frame buffer is blank
 * under a glyph cell, or solid in the dark box of an inverse 16, so the same
 * write draws dark on light or light on dark. */
static void glyph(uint8_t x, uint8_t q, bool small){
    uint8_t buf[BIG_PAGES * GLYPH_MAXW];
    if (small) q = (uint8_t)(q + 10u);       /* the widths: big 0-9, small 10-19 */
    uint16_t off = sizeof g.gw;
    for (uint8_t i = 0; i < q; i++)
        off = (uint16_t)(off + g.gw[i] * (i < 10u ? BIG_PAGES : SMALL_PAGES));
    const uint8_t w = g.gw[q], pages = small ? SMALL_PAGES : BIG_PAGES;
    A->asset_read(off, buf, (uint16_t)(w * pages));
    uint8_t *d = &A->fb[small ? SMALL_LINE : 0u][x];
    const uint8_t *b = buf;
    for (uint8_t p = 0; p < pages; p++, d += 128)
        for (uint8_t c = 0; c < w; c++) d[c] ^= *b++;
}

/* Big digits, centred, proportionally spaced. cursor: 0 none, 1 a slot after
 * the digits (as wide as a 0) left empty, 2 the slot with an underscore.
 * Inverse (light on dark) marks channel 16. */
static uint8_t bigDigits(const uint8_t *dig, uint8_t n, uint8_t cursor, bool inverse){
    uint8_t w = cursor ? g.gw[0] : (uint8_t)0u - DIG_GAP;
    for (uint8_t k = 0; k < n; k++) w = (uint8_t)(w + g.gw[dig[k]] + DIG_GAP);
    const uint8_t x0 = (uint8_t)((128u - w) / 2u);
    uint8_t x = x0;
    if (inverse) fill((uint8_t)(x - 6u), 0, (uint8_t)(x + w + 5u), BIG_PAGES * 8u - 1u, true);
    for (uint8_t k = 0; k < n; k++){
        glyph(x, dig[k], false);
        x = (uint8_t)(x + g.gw[dig[k]] + DIG_GAP);
    }
    if (cursor == 2u) fill(x, CURSOR_Y0, (uint8_t)(x + g.gw[0] - 1u), CURSOR_Y1, true);
    return x0;                         /* where the first big digit starts */
}

/* v as n digits (leading zeros kept), then the cursor slot (bigDigits). Of 3
 * or 4 digits, the first two are drawn small in front, as the IC-M94 shows
 * 1019: the 19 stays exactly where channel 19 is, a smaller 10 before it. */
static void bigNumber(uint16_t v, uint8_t n, uint8_t cursor, bool inverse){
    uint8_t dig[4];
    for (uint8_t k = 0; k < n; k++){
        const uint16_t p = P10[4u - n + k];
        uint8_t q = 0;
        while (v >= p){ v = (uint16_t)(v - p); q++; }
        dig[k] = q;
    }
    const uint8_t pre = n > 2u ? 2u : 0u;
    uint8_t x = bigDigits(dig + pre, (uint8_t)(n - pre), cursor, inverse);
    if (!pre) return;
    const uint8_t w0 = g.gw[10u + dig[0]];
    x = (uint8_t)(x - PREFIX_GAP - g.gw[10u + dig[1]] - SMALL_GAP - w0);
    glyph(x, dig[0], true);
    glyph((uint8_t)(x + w0 + SMALL_GAP), dig[1], true);
}

/* A channel designator: two digits (with the leading zero) or four. */
static void bigChannel(uint16_t d, bool inverse){ bigNumber(d, d < 100u ? 2u : 4u, 0, inverse); }

static void tag(const char *s, uint8_t x){
    A->print_inverse(s, x, 0, true, true, (uint8_t)(x + slen(s) * 4u));
}
static void tiny(const char *s, uint8_t x){ A->print_tiny(s, x, 1, true, true); }

static void draw(uint32_t now){
    const uint16_t d = CH[g.ch];
    char line[24], *o = line;

    A->display_clear();
    A->status_clear();
    if (g.tx) tag("TX", 2);
    else if (g.open) tag("RX", 2);
    else if (g.mon) tag("MON", 2);
    tiny(chLowOnly(d) || !g.hi ? "LO" : "HI", 20);
    if (chRx(d) != chTx(d)) tiny(chRxOnly(d) ? "RX ONLY" : "DUP", 32);
    A->draw_battery();

    const bool blink = g.phase != 0;
    if (g.on16 && !g.tx){
        /* the channel being received, blinking; the working one below */
        if (!g.entry) bigChannel(16u, blink);
        o = put(o, "16 BUSY  PTT ");
        o = putCh(o, d);
    } else {
        if (!g.entry) bigChannel(d, d == 16u);
        if (g.dw && d != 16u) o = put(o, g.sqlOff ? "DUAL 16: SQL OFF" : "DUAL 16");
    }
    if (g.entry)   /* the digits typed so far, then a blinking underscore */
        bigNumber(g.entryV, g.entry, blink ? 1u : 2u, false);
    if (g.msg) o = put(line, g.msg);
    *o = '\0';
    A->print_normal(line, 0, 127, 6);

    A->blit_status();
    A->blit_full();
    g.tDraw = now;
    g.dirty = false;
}

/* Shown when the VFO cannot be made a marine channel. */
static void fail(void){
    A->display_clear();
    A->status_clear();
    A->print_normal("Set the VFO to", 0, 127, 2);
    A->print_normal("156.800 FM wide", 0, 127, 3);
    A->print_normal("EXIT: quit", 0, 127, 5);
    A->blit_status();
    A->blit_full();
    while (A->get_key() != APP_KEY_EXIT){ A->delay_ms(LOOP_MS); A->backlight_update(); }
}

static bool inBand(uint32_t f){ return f >= F_LO && f <= F_HI; }

/* The VFO the app runs on: marine band, FM, wide, no codes, no compander. */
static bool vfoMarine(void){
    app_beam_channel_t c;
    A->beam_get(&c);
    return inBand(A->rx_freq()) && inBand(A->tx_freq()) && !c.modulation &&
           !c.channel_bandwidth && !c.rx_codetype && !c.tx_codetype && !c.compander;
}

/* Channel 16 on the VFO, applied by the loader when the app returns, then the
 * app again. */
static bool stageVfo(void){
    app_beam_channel_t c;
    if (!A->vfo_stage) return false;
    A->beam_get(&c);                     /* power, TX lock, ... kept */
    c.rx_frequency = F_16;
    c.tx_offset_frequency = 0;
    c.tx_offset_direction = 0;
    c.rx_codetype = c.tx_codetype = 0;
    c.modulation = 0;                    /* FM   */
    c.channel_bandwidth = 0;             /* wide */
    c.frequency_reverse = 0;
    c.compander = 0;
    c.step_setting = 5;                  /* STEP_25kHz */
    return A->handoff(APP_TITLE, NULL, 0) && A->vfo_stage(&c);
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api){
    A = api;
    /* >= 0: started by our own hand-off, after staging the VFO */
    const bool again = api->handoff_read(NULL, 0, NULL) >= 0;
    if (!vfoMarine()){
        if (again || !stageVfo()) fail();
        return;
    }

    cfg_t c;
    api->cfg_load((uint8_t *)&c, sizeof c);
    if (c.magic == CFG_MAGIC && c.ch < NCH && c.prev < NCH){
        g.ch = c.ch; g.prev = c.prev;
        g.hi = (c.flags & CFG_HI) != 0;
        g.dw = (c.flags & CFG_DW) != 0;
    } else {
        g.ch = g.prev = chFind(16u);
        g.hi = g.dw = true;
    }
    api->asset_read(0, g.gw, sizeof g.gw);     /* the big digits' widths */
    g.key = APP_KEY_INVALID;
    g.running = true;
    api->backlight_on();
    selectCh(g.ch);
    g.sqlOff = api->bk_read(REG_78) == 0;   /* set from the radio's SQL menu at launch */

    while (g.running){
        const uint32_t now = api->ticks_ms();
        uint8_t key = api->get_key();
        if (key >= APP_KEY_INVALID) key = APP_KEY_INVALID;   /* saver events */

        ptt(key == APP_KEY_PTT || (GPIOB_IDR & PTT_PIN) == 0u, now);
        if (key != g.key){
            g.key = key;
            g.tKey = now;
            /* keys do nothing while transmitting (no channel change under PTT) */
            if (key != APP_KEY_INVALID && key != APP_KEY_PTT && !g.tx) onKey(key, now);
        } else if (key != APP_KEY_INVALID && !g.tx && now - g.tKey >= REPEAT_DELAY_MS &&
                   now - g.tRepeat >= REPEAT_MS){
            const int8_t dir = api->nav_dir(key);     /* UP/DOWN held: repeat */
            if (dir){ g.tRepeat = now; step(dir); }
        }
        const bool mon = key == APP_KEY_SIDE1;
        if (mon != g.mon){ g.mon = mon; g.dirty = true; }

        if (!g.tx && g.running){
            const bool open = squelchOpen();
            if (open != g.open){ g.open = open; g.dirty = true; }
            speaker(open || g.mon);
            dualWatch(now);
        }
        if (g.entry && !g.tx && now - g.tEntry >= ENTRY_MS) entryDone();
        if (g.msg && now - g.tMsg >= MSG_MS){ g.msg = NULL; g.dirty = true; }
        const uint8_t phase = (uint8_t)((now >> 9) & 1u);   /* blinking: 16 held, entry */
        if ((g.on16 || g.entry) && phase != g.phase) g.dirty = true;
        g.phase = phase;
        if (g.dirty || now - g.tDraw >= 1000u) draw(now);

        api->battery_sample();
        api->delay_ms(LOOP_MS);
        api->backlight_update();
    }
#if MARINE_TX
    if (g.tx) txOff();
#endif
    speaker(false);
}
#endif /* HOST_TEST */
