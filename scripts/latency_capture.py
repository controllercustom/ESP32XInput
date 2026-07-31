#!/usr/bin/env python3
"""Evdev event capture with microsecond timestamps for ESP32XInput latency testing.

Captures gamepad events from /dev/input/event* and prints them with precise
timestamps that can be correlated against ESP32 UART output.

Usage:
    sudo python3 latency_capture.py --evdev /dev/input/event5
"""

import argparse
import sys
import time

try:
    from evdev import InputDevice, ecodes
except ImportError:
    print("ERROR: evdev package required. Install with: pip3 install evdev")
    sys.exit(1)


def find_gamepad_device():
    """Auto-detect gamepad device by scanning /dev/input/event*."""
    from glob import glob
    
    for path in sorted(glob("/dev/input/event[0-9]*")):
        try:
            dev = InputDevice(path)
            
            absinfo = dev.capabilities(abs=True)
            
            if ecodes.ABS_X in absinfo or ecodes.BTN_A in dev.capabilities():
                return path, dev
        
    return None, None


def main():
    parser = argparse.ArgumentParser(description="ESP32XInput evdev latency capture")
    parser.add_argument("--evdev", help="Evdev device path (auto-detect if omitted)")
    
    args = parser.parse_args()
    
    if args.evdev:
        dev_path = args.evdev
    
    else:
        print("Auto-detecting gamepad device...")
        dev_path, _ = find_gamepad_device()
        
        if dev_path is None:
            print("ERROR: No gamepad device found. Specify --evdev manually.")
            sys.exit(1)
    
    try:
        dev = InputDevice(dev_path)
    except Exception as e:
        print(f"ERROR opening {dev_path}: {e}")
        sys.exit(1)
    
    print(f"Capturing events from: {dev.name} ({dev.path})")
    print("Press Ctrl+C to stop.")
    print()
    print(f"{'Timestamp (ns)':<20} {'Type':<8} {'Code':<6} {'Value':>6}")
    print("-" * 45)
    
    try:
        for event in dev.read_loop():
            ts_ns = int(event.timestamp * 1e9)
            
            type_name = ecodes.ecodes.get(ecodes.EV_ABS, {}).get(
                event.code, str(event.type)) if event.type == ecodes.EV_ABS else \
                       ecodes.ecodes.get(ecodes.EV_KEY, {}).get(
                event.code, str(event.type)) if event.type == ecodes.EV_KEY else \
                       str(event.type)
            
            print(f"{ts_ns:<20} {type_name:<8} {event.code:<6} {event.value:>6}")
    
    except KeyboardInterrupt:
        print("\nCapture stopped.")


if __name__ == "__main__":
    main()
