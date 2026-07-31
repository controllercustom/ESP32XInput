# ESP32XInput — Agent Instructions

## Build and Compile
- **Toolchain**: Arduino CLI only (no PlatformIO). Core: `esp32:esp32` v3.3.x+.
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
Descriptors are built as `static const uint8_t` arrays in `xinput_descriptor.h` using raw bytes (no TinyUSB macros). Config descriptor length is hardcoded as `XINPUT_CONFIG_DESC_LEN` (37 bytes: 9+9+5+7+7). `buildDescriptors()` registers the custom interface via `tinyusb_enable_interface(USB_INTERFACE_CUSTOM, ...)`.

The load descriptor callback (`tusb_xinput_load_descriptor`) writes the XInput-specific descriptors (interface 0xFF/0x5D/0x01 + CS_INTERFACE type 0x24 + 2 interrupt endpoints).

Device descriptor (VID/PID, class=0xFF, etc.) is set via `tinyusb_device_config_t` passed to `tinyusb_init()`. The XInput interface is registered before `tinyusb_init()` is called.

The IN endpoint transfer uses `tud_vendor_n_write()` — the TinyUSB vendor class driver automatically matches interfaces with `bInterfaceClass=0xFF`. The OUT endpoint for rumble/LED is polled via `tud_vendor_n_available()` / `tud_vendor_n_read()`.

## Key Technical Details

- **VID/PID**: `045E:028E` (Microsoft Xbox 360) by default, configurable.
- **Report size**: 20 bytes packed.
- **Interface class**: 0xFF/0x5D/0x01 (vendor-specific, Xbox 360 protocol).
- **CS_INTERFACE descriptor type 0x24**: Required for Windows XInput driver recognition.
- **IN ep 0x81** (interrupt, 32B, 4ms), **OUT ep 0x01** (interrupt, 32B, 8ms).

## Project Structure
- **Library source**: `src/ESP32XInput.h`, `src/ESP32XInput.cpp`, `src/xinput_descriptor.h`
- **Examples** (all under `examples/`): BasicButtons, BasicGamepad, JoystickTest, FullController, RumbleFeedback
- **Tests** (under `tests/`): TestBasicFunctionality, LatencyBenchmark
- **Scripts** (under `scripts/`): run_tests.py, latency_capture.py

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
- Class-specific descriptor type **0x24** (CS_INTERFACE) between interface and endpoint descriptors is **required** for Windows XInput driver recognition — do not remove it. Payload: `0x00, 0x5D, 0x01`.
- IN ep 0x81 (interrupt, 32B, 4ms), OUT ep 0x01 (interrupt, 32B, 8ms).
- Report struct (`XInputReport`) must be exactly 20 bytes packed.
- Device descriptor class/subclass/protocol must be 0xFF/0xFF/0xFF.

## Upload Notes
- ESP32-S3 in USB device mode blocks serial upload → use `UploadMode=default` FQBN option or disconnect USB during flash.
- OTA upload: `-p <IP>` (no `-l network`). Pass empty password as `--upload-field password=""`.

## License

MIT
