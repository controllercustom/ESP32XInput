# ESP32XInput — Agent Instructions

## Build and Compile
- **Toolchain**: Arduino CLI. Core: `esp32:esp32` v3.3.x+.
- **FQBN for S3**: `esp32:esp32:esp32s3`. Must append `USBMode=default` to enable USB-OTG (TinyUSB) mode. For UART upload when USB-OTG is active, also append `UploadMode=default`. Full FQBN: `"esp32:esp32:esp32s3:USBMode=default,UploadMode=default"`.

### Library Installation (Required Before Any Compile)
The library *is* the repo itself. Examples include `<ESP32XInput.h>` — without it installed as an Arduino library, every compile fails with "No such file or directory". Two methods work:

**Alternative**: symlink into user libraries dir
```bash
ln -sf /home/pi/ESP32XInput ~/Arduino/libraries/ESP32XInput
```

**Preferred**: pass `--library` flag to every compile command
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 --library /home/pi/ESP32XInput <sketch>
```

### Stale Cache Fix
Errors about `.libsdetect.d` files → clear with `rm -rf ~/.cache/arduino/`.

## Architecture

- Three source files in `src/`: header (class + enums), implementation (USB init, I/O, timer, rumble), and raw USB descriptors.
- Dirty-flag pattern with auto-send timer for low-latency operation.
- `tud_vendor_n_write_flush()` after each write to bypass TinyUSB buffering.
- Aligned report buffer (`__attribute__((aligned(4)))`) to avoid misaligned access.

## API

```cpp
#include <ESP32XInput.h>

ESP32XInput.begin();

ESP32XInput.press(ESP32XInputClass::A);
ESP32XInput.setStickLeft(0, 100);
ESP32XInput.setHat(0);        // UP
ESP32XInput.setLeftTrigger(32768);
ESP32XInput.send();

ESP32XInput.releaseAll();
```

## USB Descriptor Implementation
Descriptors are built as `static const uint8_t` arrays in `xinput_descriptor.h` using raw bytes (no TinyUSB macros). Config descriptor length is hardcoded as `XINPUT_CONFIG_DESC_LEN` (49 bytes: 9+9+17+7+7). `buildDescriptors()` registers the custom interface via `tinyusb_enable_interface(USB_INTERFACE_CUSTOM, 40, tusb_xinput_load_descriptor)` — size parameter must be **40** (interface + vendor-specific descriptor + 2 endpoints = 9+17+7+7).

The load descriptor callback (`tusb_xinput_load_descriptor`) writes the XInput-specific descriptors: interface 0xFF/0x5D/0x01, type-0x21 vendor-specific descriptor (17 bytes matching golden Xbox 360 reference), and 2 interrupt endpoints.

Device descriptor (VID/PID, class=0xFF, etc.) is set via `tinyusb_device_config_t` passed to `tinyusb_init()`. The XInput interface is registered before `tinyusb_init()` is called.

The IN endpoint transfer uses `tud_vendor_n_write()` — the TinyUSB vendor class driver automatically matches interfaces with `bInterfaceClass=0xFF`. The OUT endpoint for rumble/LED is polled via `tud_vendor_n_available()` / `tud_vendor_n_read()`.

## Vendor Control Requests (Xbox 360 "Magic Message")

Linux `xpad` (and SDL/Windows XInput) sends a vendor IN control request during init on EP0: `bmRequestType=0xC1` (VENDOR|IN|INTERFACE), `bRequest=0x01`. Without a handler TinyUSB STALLs it and the kernel logs `unable to receive magic message: -32` (3× per connect). Respond correctly in `ESP32XInput.cpp`:
- **`wValue=0x0100`** (get-state, 20B): return the current 20-byte `XInputReport` — the kernel ignores content, SDL parses bytes 2-13 as buttons/sticks/triggers.
- **`wValue=0x0000`** (vibration caps, 8B): return `{0x00, 0x08, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00}` (both motors full-range) — used by SDL, not xpad.
- Anything else → `return false` (STALL).

**Override hook**: the ESP32 core's `tud_vendor_control_xfer_cb()` (in `esp32-hal-tinyusb.c`) is a **strong** symbol that handles WebUSB/MS-OS then forwards to the **weak** `tinyusb_vendor_control_request_cb()`. Override that weak hook (declared with `extern "C"` since it's not in any header), NOT `tud_vendor_control_xfer_cb` (would be a multiple-definition link error). `tud_control_xfer()` response buffers must be `static` (transfer completes asynchronously); return `true` for non-`CONTROL_STAGE_SETUP` stages.

## Key Technical Details

- **VID/PID**: `045E:028E` (Microsoft Xbox 360) by default, configurable.
- **Report size**: 20 bytes packed.
- **Interface class**: 0xFF/0x5D/0x01 (vendor-specific, Xbox 360 protocol).
- **Vendor-specific descriptor type 0x21** (CS_INTERFACE, 17 bytes): Required for Windows XInput driver recognition — do not remove it. Payload matches golden Xbox 360 reference: `0x00, 0x5D, 0x01` in first three data bytes.
- **IN ep 0x81** (interrupt, 32B, 4ms), **OUT ep 0x01** (interrupt, 32B, 8ms).

## Project Structure
- **Library source**: `src/ESP32XInput.h`, `src/ESP32XInput.cpp`, `src/xinput_descriptor.h`
- **Examples** (all under `examples/`): BasicButtons, BasicGamepad, JoystickTest, FullController, RumbleFeedback, AutoCycle
- **Tests** (under `tests/`): TestBasicFunctionality, LatencyBenchmark
- **Scripts** (under `scripts/`): run_tests.py, latency_capture.py

## Debugging Notes — USBMode=default Pitfalls

### Serial Console Not Available at Runtime
With `USBMode=default`, the ESP32-S3 enumerates as a pure vendor-class XInput device. There is **no CDC-ACM serial port** on USB after boot — all `Serial.println()` / `Serial.printf()` calls go nowhere. Use `Serial0` (hardware UART via CP210x bridge at `/dev/ttyUSB0`) for debug output:
```cpp
// WRONG — goes to non-existent CDC console
Serial.begin(115200);
Serial.println("hello");

// CORRECT — hardware UART on GPIO43/GPIO44 (CP210x bridge)
Serial0.begin(115200);
Serial0.printf("TS:%lld\n", esp_timer_get_time());
```

### `esp_timer_get_time()` Returns `int64_t` — Use `%lld`, Not `%lu`
On ESP32-S3 (ARM), `unsigned long` is 32-bit. Using `%lu` with `esp_timer_get_time()` truncates the timer value, causing timestamp corruption that manifests as sudden resets to small numbers mid-run:
```cpp
// WRONG — %lu truncates int64_t → corrupted timestamps after ~71min uptime or heavy call stacks
Serial0.printf("TS:%lu\n", esp_timer_get_time());

// CORRECT — %lld prints full 64-bit value
Serial0.printf("TS:%lld\n", esp_timer_get_time());
```

### USB Packet Capture via usbmon (Works on Linux)
`usbmon` + `tcpdump` **does** capture XInput interrupt-IN payloads — they appear as `Leftover Capture Data` in tshark verbose output (`tshark -V`). The kernel's `xpad` driver does NOT consume URBs before usbmon intercepts them on this system. Three verification methods work:

1. **usbmon** (tcpdump → pcap) — captures raw interrupt-IN payloads directly from the bus, no external hardware needed
2. **evdev** (`/dev/input/event5`) — reads what xpad decoded from USB IN endpoint transfers
3. **External USB analyzer (Cynthon)** — alternative for systems where usbmon doesn't expose payload data

### usbmon Capture + Verification Workflow
For end-to-end verification of XInput report payloads: run AutoCycle sketch, start jstest to keep host polling the IN endpoint, then capture with tcpdump on the correct bus. Parse pcap with tshark verbose output → Python regex on `Leftover Capture Data` lines.

```bash
# Find which bus your device is on (Bus 001 in this example)
lsusb | grep -i microsoft

# Start jstest to keep IN endpoint active, then capture for ~35s
jstest /dev/input/js0 > /tmp/jstest_output.txt &
sudo timeout 35 tcpdump -i usbmon1 -w /tmp/xinput_capture.pcap
kill %1

# Decode payloads from verbose output
tshark -r /tmp/xinput_capture.pcap -V | grep "Leftover Capture Data" > /tmp/leftover_data.txt
```

Phase markers (wButtons=0xFFF1) bracket each phase transition — strict marker requires all sticks/triggers zero.

**Verified results (baseline, 2026-08-06)**: **5349 valid XInput interrupt-IN frames** captured over 35s across **12 complete cycles** (13 marker-delimited phase groups per cycle, P0→P8). Zero issues found; all phases exact-value validated:
- Header validation: **all 5349 frames** have bMessageType=0x00, bMessageSize=0x14 ✓
- Phase markers: 143 markers (wButtons=0xFFF1), **all strict** (all sticks/triggers zero) ✓
- P1 button cycling: all **11** main buttons (START, BACK, LTHUMB, RTHUMB, LB, RB, XBOX, A, B, X, Y) present and ordered across all 12 cycles ✓
- P2 d-pad sweep: all **8 directions** present in wButtons bits 0-3, ordered subsequence 0→7 verified every cycle ✓ (note: hat-to-bits via `hatToButtons[]` in `ESP32XInput.cpp:143-152`: UP=0x01, UP_RIGHT=0x09, RIGHT=0x08, DOWN_RIGHT=0x0A, DOWN=0x02, DOWN_LEFT=0x06, LEFT=0x04, UP_LEFT=0x05; centered=8 produces 0x0000, indistinguishable from reset frames)
- P3/P4 stick sweeps: full ±29491 linear range on both axes, ±32768 extreme corner, all circle points (±9830/±14745 quadrants) — zero cross-contamination between sticks, triggers zero throughout ✓
- P5/P6 trigger ramps: exact uint8 values [0, 31, 63, 127, 191, 255, 127, 0] confirmed (formula: `value * 255 / 32768`) ✓
- P7a all-input burst: wButtons=0xF7F1, L(-16384,-16384), R(+16384,+16384), LT/RT=127 — identical across cycles (4 burst frames each) ✓
- P7b A-toggle rapid-fire: **24 PRESS↔RELEASE transitions per cycle** with zero field contamination ✓
- P0/P7c/P8 idle: strict all-zero reports throughout (624/168/624 frames across 12 cycles) ✓

Caveat for pcap analysis: `releaseAll()` (ESP32XInput.cpp:263) memsets the report and sends immediately, so a zero reset frame appears at the start of every test iteration. Treat interleaved zero frames as reset frames, not contaminations; validate phase values as ordered subsequences rather than exact sequences.

### MGX Passthrough Verified (2026-08-06)
The MGX (MAGIC-X) device is **not** a raw report passthrough — it re-encodes ESP32 reports using its own XInput descriptor (iProduct `MAGIC-X`, 64B interrupt EPs, IN interval 1ms). Capture: `/tmp/xinput_capture_mgx.pcap` → 68018 packets, 0 dropped → 34003 payload frames, 74 unique patterns. Verified with `scripts/verify_mgx_passthrough.py` → **PASS** (markers=0xF7F1, all 11 P1 buttons, 8 d-pad directions, trigger ramps, P7a burst, 755 A-toggles).

Key deltas vs direct connection:
- **Marker**: `0xF7F1` (bit 12 SET still set) instead of `0xFFF1` — MGX clears bits 12-15 in the hat/misc nibble that ESP32 sets.
- **Stick re-scaling by MGX calibration**: ESP32 ±29491 → MGX ±30315; ±9830→±14248, ±14745→±14250, ±16384→±16049/16050; LY/RY idle center = **-1** (not 0). LX/RX stay bipolar.
- **Dominant frame** (25584×): `btn=0x0000 LT=0 RT=0 LX=0 LY=-1 RX=0 RY=-1`; marker frames (1127×): `0xF7F1`; P7a burst (312×): `0xF7F1 LT=RT=127` with LX=-16050 LY=-16050 RX=16049 RY=16049.
- **evdev ground truth** (`/tmp/jstest_mgx_output.txt`): stick sweeps (±8857/±14248/±19640/±30424/±32767), trigger values (0/16513/24770/32767 ≈ uint8 0/128/193/255), and button marker burst all decoded by xpad through MGX ✓.
- Capture artifacts archived in `mydocs/`: `esp32_direct_usbmon1.pcap`/`esp32_through_mgx_usbmon1.pcap` + matching `*_leftover_data.txt`.

### Trigger Scaling Formula Confirmed
`ESP32XInput.cpp`: `value * 255 / 32768`. NOT `/128`. Produces uint8_t values `[0, 31, 63, 127, 191, 255]` for the ramp table.

### evdev Verification (Ground Truth)
For validating XInput output against expected stimulus: capture via Python `evdev` library reading `/dev/input/event5`. The kernel's `xpad` driver translates the 20-byte XInput reports into standard Linux input events. Key mappings verified on this host:
- **ABS_X** (code 0): left stick X, range ~±32768 via evdev
- **ABS_Y** (code 1): left stick Y (inverted by xpad)
- **ABS_Z** (code 2): L-trigger as uint8_t (xpad scales from uint8 in report)
- **ABS_RX** (code 3): right stick X
- **ABS_RY** (code 4): right stick Y (inverted by xpad)
- **ABS_RZ** (code 5): R-trigger as uint8_t
- **ABS_HAT0X/HAT0Y** (codes 16/17): d-pad (-1, 0, +1 axes; both zero = centered)
- **BTN_A/B/X/Y/LB/RB/etc.** (codes 304+): button press/release events

## Cross-Repo Comparison: ESP32ds4 vs ESP32XInput

| Aspect | ESP32ds4 (HID gamepad) | ESP32XInput (vendor-class XInput) |
|---|---|---|
| USB stack path | `Adafruit_TinyUSB.h` → standard HID driver (`hid-generic`) | Raw TinyUSB vendor class → kernel `xpad` driver |
| Report size | 64 bytes (incl. report ID byte) | 20 bytes packed struct, no report ID |
| Send strategy | Always-send every frame (matches real DS4 behavior) | Dirty-flag + auto-poll timer for efficiency |
| Stick range API | ±127 → maps to uint8_t internally | Full int16_t (-32768..+32767) natively |
| Trigger range API | 0-32768 → scales to uint8_t (0-255) | Direct uint8_t per XInput spec |
| Feature reports | Responds to GET_REPORT for IDs 0x02/0x12/0xA3/0x81 | No feature report mechanism (vendor class doesn't define it) |
| Touchpad support | Yes, 2-finger with state+coords encoding | Not applicable — XInput protocol has no touchpad field |
| USB capture visibility | Interrupt-IN visible to usbmon/tcpdump (standard HID path) | **Visible** via `Leftover Capture Data` in tshark -V output ✓ |

### Key Takeaway for Verification
Three verification methods are available on Linux: **(1)** usbmon pcap captures raw XInput report payloads directly from the bus, **(2)** evdev events show what xpad decoded (correlate with Serial0 telemetry by timestamp), and **(3)** external USB analyzer as fallback. The usbmon method is simplest — no extra hardware or clock synchronization needed.

## End-to-End Test Suite

### Architecture
```
ESP32-S3 ──USB──▶ Host PC  │  Python captures events via evdev (us timestamps)
     │                    │  
    UART (/dev/ttyUSB0) ▶│  Timing data from LatencyBenchmark sketch
                          │
                     Correlate by timestamp → compute latency per event
```

### How It Works
1. `run_tests.py` sends "SYNC\n" over UART; ESP32 responds with `SYNC:<esp_timer_us>`; Python records host epoch time to compute a timebase offset
2. Then sends "START\n"; `LatencyBenchmark.ino` runs 50 iterations of each event type
3. Sends timestamp + sequence number over UART for each state change: `TS:<esp_timer_us>:<seq>:<event_type>:<value>`
4. `run_tests.py` captures gamepad events via evdev with `event.timestamp()` (epoch ns)
5. Converts ESP timer (us boot) to host epoch ns using the offset, then correlates by nearest timestamp within a 50ms window
6. Latency = `evdev_timestamp - converted_esp_timestamp`, reported as min/max/median/p95 per event type

### Running the Tests
```bash
# Compile and upload LatencyBenchmark to ESP32-S3
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default" tests/LatencyBenchmark
arduino-cli compile -u -p /dev/ttyUSB0 --fqbn "esp32:esp32:esp32s3:USBMode=default,UploadMode=default" tests/LatencyBenchmark

# Run the latency test (auto-detects gamepad evdev device)
sudo python3 scripts/run_tests.py --uart /dev/ttyUSB0

# Or with explicit evdev path
sudo python3 scripts/run_tests.py --uart /dev/ttyUSB0 --evdev /dev/input/event5 --json results.json
```

### Latency Optimization
1. Immediate-send on change detection via `_dirtyFlag`
2. `tud_vendor_n_write_flush()` after each write to bypass TinyUSB buffering
3. Hardware `esp_timer` for periodic poll to handle coalesced changes (default 8ms)
4. Aligned report buffer (`__attribute__((aligned(4)))`) to avoid misaligned access
5. Non-blocking USB transfers via TinyUSB vendor class driver
6. `pollRumble()` separated from `send()` to avoid latency on the send path

## USB Protocol Details (Critical for Changes)
- Vendor class: 0xFF/0x5D/0x01 on interface 0 only.
- Class-specific descriptor type **0x21** (vendor-specific, 17 bytes) between interface and endpoint descriptors is **required** for Windows XInput driver recognition — do not remove it. Payload matches golden Xbox 360 reference: `0x00, 0x5D, 0x01` in first three data bytes.
- IN ep 0x81 (interrupt, 32B, 4ms), OUT ep 0x01 (interrupt, 32B, 8ms).
- Report struct (`XInputReport`) must be exactly 20 bytes packed.
- Device descriptor class/subclass/protocol must be 0xFF/0xFF/0xFF.

## Upload Notes
- ESP32-S3 in USB device mode blocks serial upload → use `UploadMode=default` FQBN option or disconnect USB during flash.
- OTA upload: `-p <IP>` (no `-l network`). Pass empty password as `--upload-field password=""`.

## License

MIT
