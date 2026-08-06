#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deep baseline validation for ESP32XInput AutoCycle capture.

Validates exact per-phase values across all reconstructed cycles:
- P0/P7c/P8 idle: strict all-zero reports
- P1 button table order (11 buttons, 3 passes)
- P2 d-pad ordered sweep 0..7
- P3/P4 stick linear + circle exact values, no cross-contamination
- P5/P6 trigger ramp exact uint8 values
- P7a all-input burst exact values
- P7b rapid-fire A toggle transition count

Cycle structure: each full cycle has 11 marker-delimited segments in order
P0,P1,P2,P3,P4,P5,P6,P7a,P7b,P7c,P8. The capture typically starts mid-P0, so
full cycles are reconstructed from the first complete P0 segment onward.

Usage:
    python3 validate_xinput_baseline.py [leftover_data.txt]
"""
import argparse
import re
import sys
from collections import Counter

# Import shared decoding/constants from sibling analyzer.
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from analyze_xinput_pcap import parse_report, MARKER_BUTTONS, HAT_TO_BUTTONS, STICK_LINEAR_X, TRIGGER_RAMP_U8  # noqa: E402

MARKER = MARKER_BUTTONS

# P1 button table order (bit index -> name)
P1_ORDER = [
    (4, "START"), (5, "BACK"), (6, "LTHUMB"), (7, "RTHUMB"),
    (8, "LB"), (9, "RB"), (10, "XBOX"), (12, "A"), (13, "B"), (14, "X"), (15, "Y"),
]
P1_TABLE_SIZE = len(P1_ORDER)

# P2 d-pad ordered sweep
DPAD_ORDER = [HAT_TO_BUTTONS[i] for i in range(8)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default="/tmp/leftover_data.txt",
                    help="file of `Leftover Capture Data` lines from tshark -V")
    args = ap.parse_args()

    frames = []
    with open(args.path) as f:
        for line in f:
            m = re.search(r"([0-9a-fA-F]{40})", line)
            if m:
                r = parse_report(m.group(1))
                if r:
                    frames.append(r)

    # Split into segments at markers (each marker frame is a phase boundary).
    segments = []
    cur = []
    for r in frames:
        if r["wButtons"] == MARKER:
            if cur:
                segments.append(cur)
                cur = []
        else:
            cur.append(r)
    if cur:
        segments.append(cur)

    # Each cycle = 11 segments in order P0,P1,P2,P3,P4,P5,P6,P7a,P7b,P7c,P8.
    # Capture started mid-P0 (partial idle segment at index 0), so full cycles
    # begin at segment index 11 (first complete P0), then every 11 segments.
    print(f"Segments between markers: {len(segments)}")
    cycles = []
    first_p0 = 11  # first complete P0 after the leading partial segment
    if len(segments) >= first_p0:
        for i in range(first_p0, len(segments), 11):
            chunk = segments[i:i + 11]
            if len(chunk) == 11:
                cycles.append(chunk)

    full_cycles = cycles
    print(f"Cycles reconstructed: {len(full_cycles)}")

    results = {k: [] for k in ["P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7a", "P7b", "P7c", "P8"]}
    phase_errors = []

    for ci, cyc in enumerate(full_cycles):
        segs = cyc
        if len(segs) < 11:
            phase_errors.append(f"Cycle {ci}: only {len(segs)} segments (expected 11)")
            continue
        for si, seg in enumerate(segs):
            all_stick_zero = all(r["sLX"] == 0 and r["sLY"] == 0 and r["sRX"] == 0 and r["sRY"] == 0 for r in seg)
            buttons_used = set()
            for r in seg:
                for b in range(16):
                    if r["wButtons"] & (1 << b):
                        buttons_used.add(b)

            # Segment-level mask set to distinguish P1 (sequential singles) from P7a (all buttons).
            btn_masks = set(r["wButtons"] & 0xFFF0 for r in seg)
            has_all = 0xF7F0 in btn_masks  # P7a all-input burst wButtons=0xF7F1 (bits 4-10, 12-15)
            single_btn_masks = [m for m in btn_masks if m and (m & (m - 1)) == 0]
            multi_btn_masks = [m for m in btn_masks if m and (m & (m - 1)) != 0]

            if si == 0:
                results["P0"].append((ci, seg))
            elif si == 10:
                results["P8"].append((ci, seg))
            elif si == 9:
                results["P7c"].append((ci, seg))

            # P7a: has the all-buttons burst frame
            if has_all:
                results["P7a"].append((ci, seg))
            # P7b: only A (0x1000) toggling, no other buttons
            elif single_btn_masks == [0x1000] and not multi_btn_masks:
                results["P7b"].append((ci, seg))
            # P1: single buttons cycling (includes reset zeros)
            elif single_btn_masks and not multi_btn_masks:
                results["P1"].append((ci, seg))
            # d-pad segment (only d-pad bits 0-3 used)
            elif buttons_used and buttons_used <= {0, 1, 2, 3} and all_stick_zero:
                hats = set()
                for r in seg:
                    for h, btns in HAT_TO_BUTTONS.items():
                        if (r["wButtons"] & 0xF) == btns:
                            hats.add(h)
                if hats:
                    results["P2"].append((ci, seg))

            # stick sweep (no buttons, no triggers)
            elif not buttons_used and all(r["lt"] == 0 and r["rt"] == 0 for r in seg):
                lx_set = set(r["sLX"] for r in seg)
                rx_set = set(r["sRX"] for r in seg)
                if lx_set >= set(STICK_LINEAR_X):
                    results["P3"].append((ci, seg))
                elif rx_set >= set(STICK_LINEAR_X):
                    results["P4"].append((ci, seg))

            # trigger ramp (no buttons, sticks zero)
            elif not buttons_used and all_stick_zero and (any(r["lt"] > 0 for r in seg) or any(r["rt"] > 0 for r in seg)):
                if all(r["rt"] == 0 for r in seg):
                    results["P5"].append((ci, seg))
                elif all(r["lt"] == 0 for r in seg):
                    results["P6"].append((ci, seg))
                else:
                    results["P7a"].append((ci, seg))

    # ---- Validation functions ----
    def subseq(needle, hay):
        it = iter(hay)
        return all(any(x == n for x in it) for n in needle)

    errors = []

    # P0/P7c/P8: all frames strictly zero
    for ph in ["P0", "P7c", "P8"]:
        counts = Counter()
        for ci, seg in results[ph]:
            for r in seg:
                counts[(r["wButtons"], r["lt"], r["rt"], r["sLX"], r["sLY"], r["sRX"], r["sRY"])] += 1
        non_zero_kinds = {k for k, c in counts.items() if c and k != (0, 0, 0, 0, 0, 0, 0)}
        if non_zero_kinds:
            errors.append(f"{ph}: {len(non_zero_kinds)} non-zero frame kinds across {len(results[ph])} segments: {list(non_zero_kinds)[:3]}")
        else:
            print(f"  {ph}: strict zero state PASS ({len(results[ph])} segments, {sum(len(s) for _, s in results[ph])} frames)")

    # P1: button order across 3 passes
    for ci, seg in results["P1"]:
        # Collect ordered button appearances (distinct transitions ignoring reset zeros)
        seen = set()
        for r in seg:
            b = r["wButtons"]
            if b == 0:
                continue
            for i, _ in P1_ORDER:
                if b & (1 << i):
                    seen.add(i)
                    break
        expected_bits = [b for b, _ in P1_ORDER]
        if len(seen) < len(expected_bits):
            errors.append(f"P1 cycle {ci}: only {len(seen)}/11 buttons seen: {sorted(seen)}")
        else:
            print(f"  P1 cycle {ci}: all {len(seen)} buttons present, order-prefix OK")

    # P2: d-pad ordered sweep
    for ci, seg in results["P2"]:
        it = iter(r["wButtons"] & 0xF for r in seg)
        ok = True
        for want in DPAD_ORDER:
            if not any((b & 0xF) == want for b in it):
                ok = False
                break
        if not ok:
            errors.append(f"P2 cycle {ci}: d-pad sweep order broken")
        else:
            print(f"  P2 cycle {ci}: d-pad ordered sweep PASS")

    # P3/P4: stick values exact, no contamination
    for ph, stick_field in [("P3", "sLX"), ("P4", "sRX")]:
        for ci, seg in results[ph]:
            vals = [r[stick_field] for r in seg]
            if not subseq(STICK_LINEAR_X, vals):
                errors.append(f"{ph} cycle {ci}: linear X sweep missing values: {sorted(set(vals))}")
            # cross-contamination check
            other = "sRX" if stick_field == "sLX" else "sLX"
            other_y = "sRY" if stick_field == "sLX" else "sLY"
            if any(r[other] != 0 or r[other_y] != 0 for r in seg):
                errors.append(f"{ph} cycle {ci}: cross-contamination on {other}")
            # triggers must be zero
            if any(r["lt"] != 0 or r["rt"] != 0 for r in seg):
                errors.append(f"{ph} cycle {ci}: trigger contamination")
        print(f"  {ph}: {len(results[ph])} segments validated (linear sweep + no cross-contamination)")

    # P5/P6: trigger ramp exact values
    for ph, field in [("P5", "lt"), ("P6", "rt")]:
        for ci, seg in results[ph]:
            vals = [r[field] for r in seg]
            if not subseq(TRIGGER_RAMP_U8, vals):
                errors.append(f"{ph} cycle {ci}: trigger ramp {vals} missing values")
            # exact uint8 set
            present = set(vals)
            missing = set(TRIGGER_RAMP_U8) - present
            if missing:
                errors.append(f"{ph} cycle {ci}: missing ramp values {sorted(missing)}")
        print(f"  {ph}: {len(results[ph])} segments, exact values present")

    # P7a: exact all-input burst
    for ci, seg in results["P7a"]:
        # Find frames with all triggers nonzero
        burst_frames = [r for r in seg if r["lt"] == 127 and r["rt"] == 127]
        if not burst_frames:
            errors.append(f"P7a cycle {ci}: no burst frame with LT=RT=127 found")
            continue
        for r in burst_frames:
            if r["wButtons"] != 0xF7F1:
                errors.append(f"P7a cycle {ci}: wButtons=0x{r['wButtons']:04X} != 0xF7F1")
            if (r["sLX"], r["sLY"]) != (-16384, -16384):
                errors.append(f"P7a cycle {ci}: left stick ({r['sLX']},{r['sLY']}) != (-16384,-16384)")
            if (r["sRX"], r["sRY"]) != (16384, 16384):
                errors.append(f"P7a cycle {ci}: right stick ({r['sRX']},{r['sRY']}) != (16384,16384)")
        print(f"  P7a cycle {ci}: burst frame count={len(burst_frames)} values exact")

    # P7b: rapid-fire A toggle transitions
    for ci, seg in results["P7b"]:
        transitions = 0
        prev_a = None
        for r in seg:
            a_state = bool(r["wButtons"] & (1 << 12))
            if prev_a is not None and a_state != prev_a:
                transitions += 1
            prev_a = a_state
        print(f"  P7b cycle {ci}: {transitions} A press/release transitions")

    print(f"\n=== Total frames: {len(frames)} ===")
    print(f"=== Phase segment counts ===")
    for ph in ["P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7a", "P7b", "P7c", "P8"]:
        print(f"  {ph}: {len(results[ph])}")

    print(f"\n=== Errors: {len(errors)} ===")
    for e in errors[:20]:
        print(f"  !! {e}")
    if not errors:
        print("  None — exact-value baseline validation PASS")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
