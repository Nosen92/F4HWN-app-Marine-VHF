#!/usr/bin/env python3
"""Check the channel plan built into marine_app.c against App18.

Builds test/host_channels.c with the host compiler for each build option, runs
it, and compares every selector entry with tools/app18_reference.csv (typed in
from the App18 table, independently of the formula in the app):

  - every row marked voice / rx-only is in the selector, and nothing else is
    (the vdes rows only with MARINE_VDES_VOICE=1);
  - receive frequency: the coast column, or the ship column when there is no
    coast column (single-frequency channel);
  - transmit frequency: the ship column; none when the ship column is empty;
  - 1 W channels are held at low power;
  - the selector is in numeric order, each 1xxx/2xxx right after its base;
  - keypad entry (test/host_channels.c fails otherwise): every channel can be
    typed, single digits and the prefixes 10/20 wait for the pause.

    python3 tools/check_channels.py
"""
import csv, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def mhz_to_10hz(s):
    return None if s == "-" else round(float(s) * 100000)


def reference():
    rows = {}
    with open(os.path.join(HERE, "app18_reference.csv")) as f:
        for r in csv.DictReader(line for line in f if not line.startswith("#")):
            rows[r["designator"]] = r
    return rows


def build_and_run(defines):
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "host_channels")
        cmd = ["cc", "-O2", "-Wall", "-Wno-unused-function", "-DHOST_TEST",
               *[f"-D{d}" for d in defines], "-o", exe, os.path.join(ROOT, "test", "host_channels.c")]
        subprocess.run(cmd, check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    return [tuple(int(x) for x in line.split()) for line in out.split("\n") if line]


def check(defines, vdes, low_15_17):
    ref = reference()
    got = build_and_run(defines)
    errors = []
    want = {}
    for name, r in ref.items():
        if r["app"] in ("voice", "rx-only") or (r["app"] == "vdes" and vdes):
            want[int(name)] = r
    seen = [d for d, *_ in got]
    for d in sorted(set(want) - set(seen)):
        errors.append(f"{d:02d}: in App18 as {want[d]['app']}, missing from the selector")
    for d in sorted(set(seen) - set(want)):
        errors.append(f"{d:02d}: in the selector, not a {'voice/rx-only/vdes' if vdes else 'voice/rx-only'} row")
    if len(seen) != len(set(seen)):
        errors.append("duplicate designators in the selector")
    for d, rx, tx, rx_only, low in got:
        r = want.get(d)
        if r is None:
            continue
        ship, coast = mhz_to_10hz(r["ship"]), mhz_to_10hz(r["coast"])
        exp_rx = coast if coast is not None else ship
        exp_tx = ship or 0
        if rx != exp_rx:
            errors.append(f"{d:02d}: receives {rx / 1e5:.3f}, App18 {exp_rx / 1e5:.3f}")
        if tx != exp_tx:
            errors.append(f"{d:02d}: transmits {tx / 1e5:.3f}, App18 {exp_tx / 1e5:.3f}")
        if bool(rx_only) != (r["app"] == "rx-only"):
            errors.append(f"{d:02d}: rx_only={rx_only}, App18 {r['app']}")
        exp_low = r["power"] == "1W" or (low_15_17 and d in (15, 17))
        if bool(low) != exp_low:
            errors.append(f"{d:02d}: low_only={low}, expected {exp_low}")
    key = [(d % 1000, d // 1000) for d in seen]
    if key != sorted(key):
        errors.append("selector not in numeric order")
    return got, errors


def main():
    failed = False
    for defines, vdes, low in ((["MARINE_VDES_VOICE=0"], False, False),
                               (["MARINE_VDES_VOICE=1", "MARINE_LOW_15_17=1"], True, True)):
        got, errors = check(defines, vdes, low)
        label = " ".join(defines)
        if errors:
            failed = True
            print(f"FAIL {label}: {len(got)} channels")
            for e in errors:
                print("   ", e)
        else:
            dup = sum(1 for _, rx, tx, ro, _ in got if rx != tx and not ro)
            ro = sum(1 for *_, r, _ in got if r)
            print(f"OK   {label}: {len(got)} channels ({dup} duplex, {ro} receive-only) match App18")
    if "-v" in sys.argv:
        for d, rx, tx, ro, low in build_and_run(["MARINE_VDES_VOICE=0"]):
            print(f"  {d:>4}  RX {rx / 1e5:8.3f}  TX {('---' if ro else f'{tx / 1e5:.3f}'):>8}"
                  f"{'  LOW' if low else ''}{'  DUP' if rx != tx and not ro else ''}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
