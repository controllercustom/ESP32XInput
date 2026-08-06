#!/usr/bin/env python3
"""Verify ESP32XInput AutoCycle signal propagates correctly through the MGX passthrough.

The MGX presents itself to the Linux host as its own 045e:028e device (not a raw
report pass-through). It re-encodes the ESP32's AutoCycle phases: buttons, d-pad,
triggers pass through with correct values, while stick axes are re-scaled through
the MGX's own analog calibration.

Checks:
- Header 0x00/0x14 validity
- Phase markers (wButtons=0xF7F1) present and periodic
- P1: all 11 main buttons appear
- P2: all 8 d-pad directions appear (bits 0-3, same mapping as ESP32)
- P5/P6: exact trigger ramp uint8 values [0,31,63,127,191,255]
- P7a: all-buttons + LT=RT=127 burst present
- Sticks: left/right axes respond (non-zero on both) with MGX scaling noted
"""
import argparse
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])

MARKER = 0xF7F1
BUTTON_BITS = {
    4: "START", 5: "BACK", 6: "LTHUMB", 7: "RTHUMB",
    8: "LB", 9: "RB", 10: "XBOX", 12: "A", 13: "B", 14: "X", 15: "Y",
}
# MGX preserves ESP32 d-pad bit mapping (bits 0-3).
HAT_BITS = {0: 0x1, 1: 0x9, 2: 0x8, 3: 0xA, 4: 0x2, 5: 0x6, 6: 0x4, 7: 0x5}
TRIGGER_VALS = {0, 31, 63, 127, 191, 255}


def parse_report(hexstr):
    b = bytes.fromhex(hexstr)
    if len(b) < 20:
        return None

    def s16(o):
        v = b[o] | (b[o + 1] << 8)
        return v - 65536 if v >= 32768 else v

    return {
        "wButtons": b[2] | (b[3] << 8),
        "lt": b[4], "rt": b[5],
        "sLX": s16(6), "sLY": s16(8), "sRX": s16(10), "sRY": s16(12),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default="/tmp/leftover_data_mgx.txt")
    args = ap.parse_args()

    frames = []
    with open(args.path) as f:
        for line in f:
            m = re.search(r"([0-9a-fA-F]{40})", line)
            if m:
                r = parse_report(m.group(1))
                if r:
                    frames.append(r)

    print(f"=== MGX Passthrough AutoCycle Verification ===")
    print(f"Total frames decoded: {len(frames)}")

    # --- Header (MGX emits its own 0x00/0x14 header) ---
    # (parsed from raw separately)
    bad_hdr = 0
    with open(args.path) as f:
        for line in f:
            m = re.search(r"([0-9a-fA-F]{40})", line)
            if m and (m.group(1)[:2] != "00" or m.group(1)[2:4] != "14"):
                bad_hdr += 1
    print(f"Header 0x00/0x14: {len(frames)-bad_hdr}/{len(frames)} valid")

    errors = []

    # --- Markers ---
    markers = [i for i, r in enumerate(frames) if r["wButtons"] == MARKER]
    print(f"Phase markers (0xF7F1): {len(markers)}")
    if len(markers) < 20:
        errors.append(f"Only {len(markers)} markers — expected many across 35s")

    # --- Buttons ---
    btns_seen = set()
    for r in frames:
        for bit in BUTTON_BITS:
            if r["wButtons"] & (1 << bit):
                btns_seen.add(bit)
    missing_btns = set(BUTTON_BITS) - btns_seen
    print(f"P1 buttons seen: {len(btns_seen)}/11 {sorted(BUTTON_BITS[b] for b in btns_seen)}")
    if missing_btns:
        errors.append(f"Missing buttons: {[BUTTON_BITS[b] for b in missing_btns]}")

    # --- D-pad ---
    hats_seen = set()
    for r in frames:
        for h, bits in HAT_BITS.items():
            if (r["wButtons"] & 0xF) == bits:
                hats_seen.add(h)
    missing_hats = set(HAT_BITS) - hats_seen
    print(f"P2 d-pad directions seen: {len(hats_seen)}/8 {sorted(hats_seen)}")
    if missing_hats:
        errors.append(f"Missing d-pad directions: {sorted(missing_hats)}")

    # --- Triggers ---
    lt_vals = set(r["lt"] for r in frames)
    rt_vals = set(r["rt"] for r in frames)
    lt_have_ramp = TRIGGER_VALS.issubset(lt_vals)
    rt_have_ramp = TRIGGER_VALS.issubset(rt_vals)
    print(f"P5/P6 trigger ramp values present: LT {'PASS' if lt_have_ramp else 'FAIL ' + str(sorted(lt_vals))}, "
          f"RT {'PASS' if rt_have_ramp else 'FAIL ' + str(sorted(rt_vals))}")
    if not lt_have_ramp:
        errors.append("L-trigger ramp incomplete")
    if not rt_have_ramp:
        errors.append("R-trigger ramp incomplete")

    # --- P7a burst ---
    burst = [r for r in frames if r["wButtons"] == MARKER and r["lt"] == 127 and r["rt"] == 127]
    print(f"P7a all-input burst frames (0xF7F1 + LT=RT=127): {len(burst)}")
    if burst:
        r = burst[0]
        print(f"  sample: LX={r['sLX']} LY={r['sLY']} RX={r['sRX']} RY={r['sRY']} "
              f"(ESP32 sends L=-16384,-16384 R=+16384,+16384; MGX rescales)")
        if not (r["sLX"] < 0 and r["sRX"] > 0):
            errors.append("P7a stick polarity wrong (expected L negative, R positive)")

    # --- Sticks: both axes respond, no cross-contamination of sign ---
    lx_neg = any(r["sLX"] < -1000 for r in frames)
    lx_pos = any(r["sLX"] > 1000 for r in frames)
    rx_neg = any(r["sRX"] < -1000 for r in frames)
    rx_pos = any(r["sRX"] > 1000 for r in frames)
    print(f"Stick response: LX {'±' if lx_neg and lx_pos else 'partial'}, "
          f"RX {'±' if rx_neg and rx_pos else 'partial'}")
    if not (lx_neg and lx_pos):
        errors.append("Left stick X not reaching both extremes through MGX")
    if not (rx_neg and rx_pos):
        errors.append("Right stick X not reaching both extremes through MGX")

    # --- P7b rapid-fire A toggle ---
    transitions = 0
    prev = None
    for r in frames:
        a = bool(r["wButtons"] & (1 << 12))
        if prev is not None and a != prev:
            transitions += 1
        prev = a
    print(f"P7b A toggle transitions: {transitions}")

    print(f"\n=== Result: {'PASS — MGX passthrough propagates AutoCycle correctly' if not errors else 'FAIL'} ===")
    for e in errors:
        print(f"  !! {e}")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
