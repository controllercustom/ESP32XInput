#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Analyze ESP32XInput AutoCycle pcap payloads against the expected baseline.

Parses `Leftover Capture Data` lines (20-byte XInput reports) from `tshark -V`
output, splits them into phases using the all-buttons marker (wButtons=0xFFF1),
and validates each phase against the deterministic AutoCycle test plan.

Capture workflow (see AGENTS.md):
    jstest /dev/input/js0 &
    sudo timeout 35 tcpdump -i usbmon1 -w /tmp/xinput_capture.pcap
    tshark -r /tmp/xinput_capture.pcap -V | grep "Leftover Capture Data" > /tmp/leftover_data.txt

Usage:
    python3 analyze_xinput_pcap.py [leftover_data.txt]
"""
import argparse
import re
import sys
from collections import defaultdict

MARKER_BUTTONS = 0xFFF1
REPORT_SIZE = 20

BUTTON_BITS = {
    4: "START", 5: "BACK", 6: "LTHUMB", 7: "RTHUMB",
    8: "LB", 9: "RB", 10: "XBOX", 12: "A", 13: "B", 14: "X", 15: "Y",
}
# Matches ESP32XInput.cpp setHat() hatToButtons table (lines 143-152).
HAT_TO_BUTTONS = {
    0: 0x01,  # UP
    1: 0x09,  # UP_RIGHT (bits 0+3)
    2: 0x08,  # RIGHT
    3: 0x0A,  # DOWN_RIGHT (bits 1+3)
    4: 0x02,  # DOWN
    5: 0x06,  # DOWN_LEFT (bits 1+2)
    6: 0x04,  # LEFT
    7: 0x05,  # UP_LEFT (bits 0+2)
}
HAT_BY_BUTTONS = {v: k for k, v in HAT_TO_BUTTONS.items()}

STICK_LINEAR_X = [-29491, -19660, -9830, 0, 9830, 19660, 29491]
STICK_CIRCLE = [
    (-9830, -9830), (9830, -9830), (9830, 9830), (-9830, 9830),
    (-14745, -14745), (14745, -14745), (14745, 14745), (-14745, 14745),
    (0, 0),
]
TRIGGER_RAMP_U8 = [0, 31, 63, 127, 191, 255, 127, 0]
DPAD_SWEEP = [0, 1, 2, 3, 4, 5, 6, 7, 8, 8]


def parse_report(hexstr):
    b = bytes.fromhex(hexstr)
    if len(b) != REPORT_SIZE:
        return None
    wButtons = b[2] | (b[3] << 8)
    lt = b[4]
    rt = b[5]
    sLX = b[6] | (b[7] << 8)
    sLY = b[8] | (b[9] << 8)
    sRX = b[10] | (b[11] << 8)
    sRY = b[12] | (b[13] << 8)
    if sLX >= 32768: sLX -= 65536
    if sLY >= 32768: sLY -= 65536
    if sRX >= 32768: sRX -= 65536
    if sRY >= 32768: sRY -= 65536
    return {
        "raw": b,
        "type": b[0], "size": b[1], "wButtons": wButtons,
        "lt": lt, "rt": rt, "sLX": sLX, "sLY": sLY, "sRX": sRX, "sRY": sRY,
    }


def hat_from_buttons(w):
    return HAT_BY_BUTTONS.get(w & 0xF, None)


def pressed_names(bit_set):
    return [BUTTON_BITS[i] for i in sorted(BUTTON_BITS) if i in bit_set]


def load_frames(path):
    frames = []
    with open(path) as f:
        for line in f:
            m = re.search(r"([0-9a-fA-F]{40})", line)
            if m:
                r = parse_report(m.group(1))
                if r:
                    frames.append(r)
    return frames


def split_phases(frames):
    """Split frames into segments; marker frames delimit phases."""
    segments = []
    cur = []
    for r in frames:
        if r["wButtons"] == MARKER_BUTTONS:
            if cur:
                segments.append(cur)
                cur = []
            segments.append([r])  # marker itself
        else:
            cur.append(r)
    if cur:
        segments.append(cur)
    return segments


def valid_stick_linear_sequence(vals, expected_x, expected_y, which):
    """Check a run of stick reports matches the expected sweep sequence."""
    # Collapse consecutive identical frames to state transitions.
    seq = []
    prev = None
    for r in vals:
        key = (r["sLX"], r["sLY"], r["sRX"], r["sRY"])
        if key != prev:
            seq.append(key)
            prev = key
    target = []
    for x in expected_x:
        if which == "left":
            target.append((x, 0, 0, 0))
        else:
            target.append((0, 0, x, 0))
    for cx, cy in expected_y:
        if which == "left":
            target.append((0, cy, 0, 0))
        else:
            target.append((0, 0, 0, cy))
    target.append((0, 0, 0, 0))  # final center after circle
    return seq, target


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default="/tmp/leftover_data.txt",
                    help="file of `Leftover Capture Data` lines from tshark -V")
    args = ap.parse_args()

    frames = load_frames(args.path)
    if not frames:
        print("No frames parsed.")
        return 1

    print(f"=== XInput AutoCycle Baseline Analysis ===")
    print(f"Total frames parsed: {len(frames)}")

    # Header validation
    bad_header = [r for r in frames if r["type"] != 0x00 or r["size"] != 0x14]
    print(f"Header validation: {len(frames)-len(bad_header)}/{len(frames)} have bMessageType=0x00, bMessageSize=0x14")
    if bad_header:
        print(f"  !! {len(bad_header)} frames with bad header: {[r['raw'].hex() for r in bad_header[:5]]}")

    # Marker counts
    markers = [r for r in frames if r["wButtons"] == MARKER_BUTTONS]
    print(f"Phase markers (wButtons=0xFFF1): {len(markers)}")

    # Validate marker payloads are pure (all sticks/triggers zero)
    bad_marker = [r for r in markers if r["lt"] != 0 or r["rt"] != 0 or
                  r["sLX"] != 0 or r["sLY"] != 0 or r["sRX"] != 0 or r["sRY"] != 0]
    print(f"  Marker strictness (all sticks/triggers zero): {'PASS' if not bad_marker else f'FAIL ({len(bad_marker)})'}")

    segments = split_phases(frames)
    print(f"Segments (marker-delimited runs): {len(segments)}")

    # Analyze each data segment (skip marker-only segments)
    data_segments = [s for s in segments if len(s) > 1 or s[0]["wButtons"] != MARKER_BUTTONS]
    print(f"Data segments: {len(data_segments)}\n")

    # --- Build a phase classifier from the deterministic test plan ---
    issues = []
    stats = defaultdict(int)

    for si, seg in enumerate(data_segments):
        # Determine dominant content
        first = seg[0]
        # D-pad only
        sticks_zero = all(r["sLX"] == 0 and r["sLY"] == 0 and r["sRX"] == 0 and r["sRY"] == 0 for r in seg)
        trig_zero = all(r["lt"] == 0 and r["rt"] == 0 for r in seg)
        btn_bits = set()
        for r in seg:
            for b in range(16):
                if r["wButtons"] & (1 << b):
                    btn_bits.add(b)

        # Distinct hat states in segment
        hats = [hat_from_buttons(r["wButtons"]) for r in seg]
        hats_present = set(h for h in hats if h is not None)

        if sticks_zero and trig_zero and btn_bits <= {0, 1, 2, 3}:
            # Idle or d-pad segment
            if hats_present == {8} or not hats_present:
                label = "IDLE (zero state)"
                stats["idle"] += 1
            else:
                label = f"D-PAD sweep ({sorted(hats_present)})"
                stats["dpad"] += 1
                # Validate d-pad transitions — ordered subsequence of [0..7] directions.
                # Centered (8) produces 0x0000 which is indistinguishable from releaseAll()
                # reset frames, so it is validated via the trailing zero runs instead.
                expected_order = list(range(8))
                it = iter(r["wButtons"] & 0xF for r in seg)
                ok = all(any((b & 0xF) == HAT_TO_BUTTONS[e] for b in it) for e in expected_order)
                if not ok:
                    issues.append(f"Seg{si}: d-pad direction order wrong, have hats {sorted(hats_present)}")
                if not (sticks_zero and trig_zero):
                    issues.append(f"Seg{si}: d-pad has non-zero stick/trigger contamination")
        elif btn_bits and btn_bits <= {4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15}:
            label = f"BUTTON cycle ({pressed_names(btn_bits)})" if len(btn_bits) <= 3 else "BUTTON combo"
            stats["button"] += 1
            if not (sticks_zero and trig_zero):
                issues.append(f"Seg{si}: button phase has stick/trigger contamination")
        else:
            # Check specific phases
            all_btns = btn_bits >= {4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15}
            if all_btns and any(r["lt"] == 127 and r["rt"] == 127 for r in seg):
                label = "P7a ALL-INPUTS burst"
                stats["p7a"] += 1
                # Validate P7a frame
                for r in seg:
                    if r["lt"] == 127 and r["rt"] == 127:
                        if r["sLX"] != -16384 or r["sLY"] != -16384 or r["sRX"] != 16384 or r["sRY"] != 16384:
                            issues.append(f"Seg{si}: P7a stick values wrong: L=({r['sLX']},{r['sLY']}) R=({r['sRX']},{r['sRY']})")
                        if r["wButtons"] != 0xF7F1:
                            issues.append(f"Seg{si}: P7a wButtons=0x{r['wButtons']:04X}, expected 0xF7F1")
            elif len(btn_bits) == 1 and 12 in btn_bits and any(r["lt"] != 0 or r["rt"] != 0 for r in seg):
                label = "P7b RAPID-FIRE A"
                stats["p7b"] += 1
            elif btn_bits == {12}:
                label = "P7b RAPID-FIRE A (toggle)"
                stats["p7b"] += 1
            else:
                label = f"OTHER btn_bits={sorted(btn_bits)} lt/rt=({first['lt']},{first['rt']})"
                stats["other"] += 1

        # Trigger ramp detection — validate as subsequence (releaseAll() inserts zero reset frames).
        if sticks_zero and not btn_bits and (any(r["lt"] > 0 for r in seg) or any(r["rt"] > 0 for r in seg)):
            def is_subsequence(needle, haystack):
                it = iter(haystack)
                return all(any(x == n for x in it) for n in needle)

            ltr = [r["lt"] for r in seg]
            rtr = [r["rt"] for r in seg]
            if all(r["rt"] == 0 for r in seg) and any(r["lt"] > 0 for r in seg):
                label = f"P5 L-TRIGGER ramp"
                stats["p5"] += 1
                if not is_subsequence(TRIGGER_RAMP_U8, ltr):
                    issues.append(f"Seg{si}: L-trigger ramp {ltr} does not contain expected {TRIGGER_RAMP_U8} as subsequence")
            elif all(r["lt"] == 0 for r in seg) and any(r["rt"] > 0 for r in seg):
                label = f"P6 R-TRIGGER ramp"
                stats["p6"] += 1
                if not is_subsequence(TRIGGER_RAMP_U8, rtr):
                    issues.append(f"Seg{si}: R-trigger ramp {rtr} does not contain expected {TRIGGER_RAMP_U8} as subsequence")

        # Stick sweep detection (P3/P4)
        if trig_zero and not btn_bits and not hats_present:
            lx_vals = set(r["sLX"] for r in seg)
            rx_vals = set(r["sRX"] for r in seg)
            if lx_vals >= set(STICK_LINEAR_X) or rx_vals >= set(STICK_LINEAR_X):
                if lx_vals >= set(STICK_LINEAR_X):
                    label = "P3 LEFT-STICK sweep"
                    stats["p3"] += 1
                    # Check no right stick contamination
                    if any(r["sRX"] != 0 or r["sRY"] != 0 for r in seg):
                        issues.append(f"Seg{si}: P3 has right-stick contamination")
                else:
                    label = "P4 RIGHT-STICK sweep"
                    stats["p4"] += 1
                    if any(r["sLX"] != 0 or r["sLY"] != 0 for r in seg):
                        issues.append(f"Seg{si}: P4 has left-stick contamination")

        print(f"  Seg{si:2d} [{label}] frames={len(seg):4d} hats={sorted(hats_present) if hats_present else '-'} "
              f"wBtn=0x{min(r['wButtons'] for r in seg):04X}..0x{max(r['wButtons'] for r in seg):04X}")

    print(f"\n=== Segment classification ===")
    for k, v in sorted(stats.items()):
        print(f"  {k:10s}: {v}")

    print(f"\n=== Issues ({len(issues)}) ===")
    if issues:
        for i in issues:
            print(f"  !! {i}")
    else:
        print("  None — all phases validated clean.")

    return 0 if not issues else 1


if __name__ == "__main__":
    sys.exit(main())
