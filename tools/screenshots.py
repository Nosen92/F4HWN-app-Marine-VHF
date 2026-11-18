#!/usr/bin/env python3
"""Run the built Marine VHF app in the firmware's overlay-app emulator.

App/apps/app_screenshot.py (firmware repository) runs an app's ELF in Unicorn
on top of the built Fusion firmware, with the firmware's real drawing services.
This driver adds the services Marine VHF needs (beam_get, tx_state, handoff,
vfo_stage, tx_power), a squelch that opens when the app is tuned to a busy
frequency, and a log of what the app does to the radio, then:

  - saves screenshots of the main states (PNG, --out);
  - checks the radio sequences: the dual-watch look (period, time away from
    the working channel), the TX key-up order, the VFO staging.

Needs the firmware built (build/Fusion/f4hwn.fusion.elf), the app built
(App/apps/marine/marine.elf) and: pip install unicorn pyelftools

    python3 tools/screenshots.py --fw ../uv-k1-k5v3-firmware-custom --out docs/img
"""
import argparse, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--fw", required=True, help="firmware repository (built Fusion preset)")
ap.add_argument("--out", default=os.path.join(ROOT, "docs", "img"))
args = ap.parse_args()

sys.path.insert(0, os.path.join(args.fw, "App", "apps"))
import app_screenshot as S  # noqa: E402

APP_DIR = os.path.join(args.fw, "App", "apps", "marine")
FIRMWARE = os.path.join(args.fw, "build", "Fusion", "f4hwn.fusion.elf")


def channel_list():
    """The selector, from the host build of the app's own table."""
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "hc")
        subprocess.run(["cc", "-O2", "-DHOST_TEST", "-o", exe,
                        os.path.join(ROOT, "test", "host_channels.c")], check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    return [int(line.split()[0]) for line in out.splitlines()]


CH = channel_list()


def cfg(ch, prev=16, hi=True, dw=True):
    return bytes([0xA7, CH.index(ch), CH.index(prev), (1 if hi else 0) | (2 if dw else 0)])


def mhz(f):
    return f"{f / 1e5:.3f}"


class Marine(S.Emu):
    """The emulator, with the services this app uses and a radio log."""

    def __init__(self, scenario):
        super().__init__(APP_DIR, FIRMWARE, scenario)
        self.log = []          # (T ms, event, value)
        self.bk[0x78] = scenario.get("reg78", 0x5048)   # squelch thresholds (0: SQL off)
        # PTT on its pin (PB10, GPIOB_IDR bit 10, low when pressed): held while a PTT key
        # event of the scenario lasts, or over "ptt_hold" (start, end) ms
        self.u.hook_add(S.UC_HOOK_MEM_READ, self.on_ptt_pin, begin=0x50000410, end=0x50000413)

    def on_ptt_pin(self, u, access, addr, size, value, data):
        t0, t1 = self.sc.get("ptt_hold", (0, 0))
        down = self.key_now() == S.KEYS["PTT"] or t0 <= self.T < t1
        u.mem_write(0x50000410, struct.pack("<I", 0 if down else 0x400))

    def tuned_now(self):
        return self.bk.get(0x38, 0) | self.bk.get(0x39, 0) << 16

    def busy(self, f):
        return any(f == bf and t0 <= self.T < t1 for bf, t0, t1 in self.sc.get("busy", []))

    def on_stub(self, u, addr, size, data):
        name = self.stubs.get(addr)
        sc = self.sc
        if name == "beam_get":
            ch = bytearray(44)
            struct.pack_into("<I", ch, 0, sc.get("freq", 15680000))
            ch[12] = sc.get("modulation", 0)         # modulation
            ch[16] = 7                               # output_power HIGH
            ch[17] = sc.get("bandwidth", 0)          # channel_bandwidth
            u.mem_write(self.r(0), bytes(ch))
            self.ret(0)
        elif name == "tx_state":
            self.ret(sc.get("tx_state", 0))
        elif name in ("handoff", "vfo_stage"):
            if name == "vfo_stage":
                f = struct.unpack("<I", bytes(u.mem_read(self.r(0), 4)))[0]
                self.log.append((self.T, name, f))
            else:
                self.log.append((self.T, name, bytes(u.mem_read(self.r(0), 16)).split(b"\0")[0].decode()))
            self.ret(1)
        elif name in ("tx_power", "tx_prepare", "tx_carrier", "tx_end", "play_tone", "tones_off_rx"):
            self.log.append((self.T, name, self.r(0)))
            if name == "tx_prepare":              # as RADIO_PrepareTx: PA bias set, PA_ENABLE off
                self.bk[0x36] = 0x9F88
            if name == "play_tone":
                self.tick(self.r(1))
            self.ret(0)
        elif name == "bk_write":
            reg, val = self.r(0) & 0xFF, self.r(1) & 0xFFFF
            super().on_stub(u, addr, size, data)
            if reg == 0x39:
                self.log.append((self.T, "freq", self.tuned_now()))
            elif reg in (0x30, 0x36):
                self.log.append((self.T, f"reg{reg:02x}", val))
        elif name == "bk_read" and (self.r(0) & 0xFF) == 0x0C:
            self.tick(0.03)
            self.ret(2 if self.busy(self.tuned_now()) else 0)
        else:
            super().on_stub(u, addr, size, data)


def run(label, **sc):
    sc.setdefault("shots", [("main", 800)])
    sc.setdefault("freq", 15680000)          # the VFO: 156.800 MHz, FM wide
    e = Marine(sc).run()
    os.makedirs(args.out, exist_ok=True)
    for shot, t, rows in e.taken:
        path = os.path.join(args.out, f"{label}-{shot}.png" if len(e.taken) > 1 else f"{label}.png")
        S.png(rows, path, scale=4)
        print(f"  {path}")
    return e


failures = []


def expect(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        failures.append(what)


K = S.KEYS
print("screens:")
run("ch16")                                                   # no config yet: 16, full power
run("ch72", cfg=cfg(72))
run("ch1019-low", cfg=cfg(1019, hi=False))
run("ch2019-rxonly", cfg=cfg(2019), keys=[(500, K["PTT"], 600)], shots=[("ptt", 700)])
run("ch20-duplex-rx", cfg=cfg(20), busy=[(16160000, 0, 9e9)], shots=[("main", 900)])
run("entry", cfg=cfg(72), keys=S.presses(700, "1", "0", "1"), shots=[("a", 1300)])
run("tx", cfg=cfg(72), keys=[(500, K["PTT"], 1000)], shots=[("main", 1000)])

print("four-digit channels (IC-M94 style):")
def frame(e):
    return e.taken[-1][2][8:56]          # frame-buffer lines 0-5: the channel area
def ink_cols(rows):
    return [x for x in range(128) if any(r[x] for r in rows)]
f19, f1019, f2019 = (frame(run(n, cfg=cfg(c))) for n, c in (("ch19", 19), ("ch1019", 1019), ("ch2019", 2019)))
x19 = ink_cols(f19)[0]
same = all(r1[x19 - 1:] == r2[x19 - 1:] for r1, r2 in zip(f19, f1019)) and \
       all(r1[x19 - 1:] == r2[x19 - 1:] for r1, r2 in zip(f19, f2019))
expect(same, f"the big 19 of 1019 and 2019 is pixel for pixel the 19 of channel 19 (from column {x19})")
pre = [x for x in ink_cols(f1019) if x < x19 - 1]
rows_pre = [y for y, r in enumerate(f1019) if any(r[x] for x in pre)]
expect(pre and max(pre) < x19 - 3 and rows_pre and min(rows_pre) >= 24 and max(rows_pre) == 45,
       f"a small 10 in front: columns {min(pre)}-{max(pre)}, rows {min(rows_pre)}-{max(rows_pre)} "
       f"(the big digits end on row 45)")
for c in (2020, 2078, 2079, 1027, 1028):
    xs = ink_cols(frame(run(f"ch{c}", cfg=cfg(c))))
    expect(xs[0] >= 2, f"{c}: the prefix fits on the screen (from column {xs[0]})")

print("dual watch:")
e = run("dw16", cfg=cfg(72), busy=[(15680000, 3000, 6000)],
        shots=[("a", 4600), ("b", 4860), ("back", 9000)])
f72 = 15662500
tunes = [(t, v) for t, ev, v in e.log if ev == "freq"]
beeps = [t for t, ev, v in e.log if ev == "play_tone"]
to16 = [t for t, v in tunes if v == 15680000]
# a look: tuned to 16, then straight back to 72 (no signal there)
looks = [(t0, t1) for (t0, f0), (t1, f1) in zip(tunes, tunes[1:])
         if f0 == 15680000 and f1 == f72 and not any(t0 <= b <= t1 for b in beeps)]
starts = [0.0] + [t for t in to16 if t < 3000]
periods = [b - a for a, b in zip(starts, starts[1:])]
expect(periods and max(periods) <= 2000, f"16 sampled every {sorted(set(round(p) for p in periods))} ms (<= 2000)")
away = [t1 - t0 for t0, t1 in looks]
expect(away and max(away) < 150, f"each look at 16 leaves 72 for {max(away):.0f} ms (< 150)")
expect(beeps and 3000 <= beeps[0] <= 3000 + 2000, f"signal on 16 from 3000 ms caught at {beeps[0]:.0f} ms (2 beeps)")
back = [t for t, v in tunes if v == f72 and t > 6000]
expect(back and 7900 <= back[0] <= 8200, f"back on 72 at {back[0]:.0f} ms (signal ended 6000 + hang 2000)")

e = run("dw-sql-off", cfg=cfg(72), reg78=0, busy=[(15680000, 0, 9e9)], shots=[("main", 5000)])
to16 = [t for t, ev, v in e.log if ev == "freq" and v == 15680000]
expect(not to16, "squelch off (SQL 0): no look at 16, 'DUAL 16: SQL OFF' shown")

print("keys:")
e = run("nav", cfg=cfg(72), keys=S.presses(500, "UP", "DOWN", "DOWN"), shots=[("main", 1500)])
seq = [round(v / 100) for t, ev, v in e.log if ev == "freq" and t > 400]
# nav_dir (firmware): UV-K1 layout (SetNav = K1, as in the emulator) KEY_UP is LEFT = -1
expect(seq == [156575, 156625, 156675], f"UP, DOWN, DOWN from 72 (nav_dir, K1 layout): {seq} (71, 72, 73)")
def tuned(e, t0, t1=9e9):
    """(T, MHz x 1000) of each tune between t0 and t1, looks at 16 left out"""
    return [(round(t), round(v / 100)) for t, ev, v in e.log
            if ev == "freq" and t0 <= t < t1 and v != 15680000]

e = run("entry69", cfg=cfg(72), keys=S.presses(500, "6", "9"), shots=[("main", 1200)])
seq = [f for t, f in tuned(e, 400)]
expect(seq[:1] == [156475], f"keys 6, 9 select channel 69 at the 9: {seq[:1]}")
e = run("entry1", cfg=cfg(72), keys=S.presses(500, "1"), shots=[("typing", 1000), ("main", 1800)])
seq = tuned(e, 400)
expect(len(seq) == 1 and seq[0][1] == 160650 and 1490 <= seq[0][0] <= 1560,
       f"key 1, then a 1 s pause: channel 01 (RX 160.650) at {seq[0][0] if seq else None} ms")
e = run("entry3", cfg=cfg(72), keys=S.presses(500, "3"), shots=[("main", 900)])
seq = tuned(e, 400)
expect(seq[:1] and seq[0][1] == 160750 and seq[0][0] < 560, f"key 3: channel 03 at once (no 3x channel): {seq[:1]}")
e = run("entry1019", cfg=cfg(72), keys=S.presses(500, "1", "0", "1", "9"), shots=[("typing", 1100), ("main", 1600)])
seq = tuned(e, 400)
expect([f for t, f in seq] == [156950] and seq[0][0] < 1300, f"keys 1, 0, 1, 9: channel 1019 at the 9: {seq}")
e = run("entry-ptt", cfg=cfg(72), keys=S.presses(500, "1") + [(700, K["PTT"], 1500)], shots=[("main", 2600)])
seq = tuned(e, 400)
expect(all(f in (156625, 156625) for t, f in seq), f"PTT while typing: entry dropped, still on 72: {seq}")

print("UP/DOWN held:")
e = run("hold", cfg=cfg(72), keys=[(500, K["DOWN"], 2000)], shots=[("main", 2700)])
seq = tuned(e, 400, 2600)
times = [t for t, f in seq]
gaps = [b - a for a, b in zip(times[1:], times[2:])]
expect(times and times[0] < 560 and len(times) > 1 and 1490 <= times[1] <= 1560,
       f"one step on the press ({times[:1]}), the next 1 s later ({times[1:2]})")
expect(gaps and max(gaps) <= 160 and min(gaps) >= 140, f"then a step every {sorted(set(gaps))} ms while held")
chans = [CH[(CH.index(72) + i + 1) % len(CH)] for i in range(len(seq))]
expect(len(seq) == 8, f"held 2 s: {len(seq)} steps, 72 -> {chans[-1] if chans else None}")
e = run("menu16", cfg=cfg(1078, prev=16), keys=S.presses(500, "MENU", "MENU"), shots=[("main", 1200)])
seq = [round(v / 100) for t, ev, v in e.log if ev == "freq" and t > 400]
expect(seq == [156800, 156925], f"MENU: 16, MENU again: back to 1078: {seq}")

print("PTT with a key:")
e = run("ptt-key", cfg=cfg(72), ptt_hold=(500, 1500), keys=[(900, K["UP"], 150)], shots=[("main", 2000)])
ev = [(n, round(t)) for t, n, v in e.log if n in ("tx_carrier", "tx_end")]
f_tx = sorted({v for t, n, v in e.log if n == "freq" and 500 <= t < 1500})
expect([n for n, t in ev] == ["tx_carrier", "tx_end"] and ev[1][1] >= 1500,
       f"UP pressed under PTT: one key-up, one tx_end at release ({ev})")
expect(f_tx == [15662500], f"no channel change under PTT: TX stayed on 72 ({[mhz(f) for f in f_tx]})")

print("transmit:")
e = run("tx-seq", cfg=cfg(1, hi=False), keys=[(500, K["PTT"], 1000)], shots=[("main", 2000)])
seq = [(ev, v) for t, ev, v in e.log if 500 <= t <= 1600 and ev in ("tx_power", "tx_prepare", "freq", "reg30", "reg36", "tx_carrier", "tx_end")]
names = [ev for ev, v in seq]
i_prep, i_car = names.index("tx_prepare"), names.index("tx_carrier")
tx_f = [v for ev, v in seq[i_prep:i_car] if ev == "freq"]
expect(names[0] == "tx_power" and dict(seq)["tx_power"] == 4, "low power requested before tx_prepare (LOW4)")
expect(tx_f == [15605000], f"TX tuned to the ship leg of 01, {', '.join(mhz(f) for f in tx_f)} MHz, before the PA is keyed")
expect(("reg30", 0xC1FE) in seq[i_prep:i_car], "synthesizer restarted in TX (REG_30 0xC1FE) before the PA")
pa = [v for ev, v in seq[i_prep:i_car] if ev == "reg36"]
i_f = next(i for i, (ev, v) in enumerate(seq) if ev == "freq" and i > i_prep)
i_pa0 = seq.index(("reg36", 0))
expect(pa == [0, 0x9F88] and i_pa0 < i_f, "PA drive off while retuning, restored before PA_ENABLE")
expect("tx_end" in names[i_car:], "tx_end on PTT release")
rx_after = [v for t, ev, v in e.log if ev == "freq" and t > 1500]
expect(rx_after[:1] == [16065000], f"back to receive on the coast leg of 01: {mhz(rx_after[0])} MHz")

e = run("tx16-high", cfg=cfg(72, hi=False), keys=S.presses(400, "MENU") + [(800, K["PTT"], 300)], shots=[("main", 1500)])
pw = [v for t, ev, v in e.log if ev == "tx_power"]
expect(pw == [7], f"MENU selects 16 at full power: tx_power({pw})")
e = run("tx75", cfg=cfg(75), keys=[(500, K["PTT"], 300)], shots=[("main", 1200)])
pw = [v for t, ev, v in e.log if ev == "tx_power"]
expect(pw == [4], f"75 held at low power with HI selected: tx_power({pw})")

print("VFO staging:")
e = run("stage", freq=14450000, shots=[("main", 300)])
ev = [(n, v) for t, n, v in e.log if n in ("handoff", "vfo_stage")]
expect(ev == [("handoff", "Marine VHF"), ("vfo_stage", 15680000)] and e.returned,
       f"VFO on 144.500: {ev}, returned={e.returned}")
e = run("stage-failed", freq=14450000, handoff=b"", handoff_from="Marine VHF",
        keys=[(1000, K["EXIT"], 200)], shots=[("main", 600), ("end", 1400)])
expect(not [1 for t, n, v in e.log if n == "vfo_stage"] and e.returned, "already staged once: message, no second hand-off")
e = run("am-vfo", freq=15680000, modulation=1, shots=[("main", 300)])
expect([n for t, n, v in e.log if n == "vfo_stage"] == ["vfo_stage"], "VFO on 156.800 AM: staged to FM")

print()
print("all checks passed" if not failures else f"{len(failures)} check(s) failed")
sys.exit(1 if failures else 0)
