# ESP32XInput

Emulate an Xbox 360 wired controller over USB on ESP32-S2/S3 using TinyUSB. No external USB chip needed — uses the built-in USB-OTG peripheral.

## Features

- Emulates Xbox 360 controller (VID `045e` / PID `028e`)
- 15 buttons (DPAD, ABXY, shoulders, thumbs, start/back/Xbox)
- 2 analog sticks (-32767..32767)
- 2 analog triggers (0-32768 input, mapped to 0-255)
- Rumble motor command reception (OUT endpoint polling)
- LED index notification
- Configurable VID/PID (defaults to Microsoft Xbox 360)
- Configurable USB poll rate (default 8ms)
- Dirty-flag optimization with auto-send timer
- Measured stick/trigger latency: **median ~4.5ms** on Linux xpad driver

## Requirements

- **MCU**: ESP32-S2 or ESP32-S3 with USB-OTG peripheral
- **Arduino Core**: `esp32` v3.3.x+
- **Toolchain**: Arduino CLI (no PlatformIO needed)

## Quick Start

```cpp
#include <ESP32XInput.h>

void setup() {
    ESP32XInput.begin();
}

void loop() {
    ESP32XInput.press(ESP32XInput.Button::A);
    delay(100);
    ESP32XInput.release(ESP32XInput.Button::A);
    delay(100);
}
```

## API Reference

### Core Methods

| Method | Description |
|--------|-------------|
| `begin(vid, pid)` | Initialize USB stack. Optional custom VID/PID. |
| `send()` | Send current state to host (alias for `update()`). |
| `ready()` | Returns true when USB is mounted (alias for `isConnected()`). |
| `releaseAll()` | Zero all inputs and send neutral report. |
| `setPollInterval(ms)` | Set auto-send interval (default 8, minimum 4). |
| `update()` | Force immediate dirty-report send. |
| `pollRumble()` | Poll OUT endpoint for rumble/LED commands. |

### Buttons

| Method | Description |
|--------|-------------|
| `press(btn)` | Sets a button as pressed. |
| `release(btn)` | Sets a button as released. |
| `setButton(btn, state)` | Sets button state directly. |
| `getButton(btn)` | Returns current button state. |

**Button Constants**: `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`, `START`, `BACK`, `LEFT_THUMB`, `RIGHT_THUMB`, `LEFT_SHOULDER`, `RIGHT_SHOULDER`, `XBOX`, `A`, `B`, `X`, `Y`.

### Analog Input

| Method | Description |
|--------|-------------|
| `setStickLeft(x, y)` | Left stick, -32767..32767. |
| `setStickRight(x, y)` | Right stick, -32767..32767. |
| `setLeftTrigger(value)` | Left trigger, 0..32768 (mapped to 0-255). |
| `setRightTrigger(value)` | Right trigger, 0..32768 (mapped to 0-255). |

### D-Pad

| Method | Description |
|--------|-------------|
| `setHat(hat)` | Set d-pad direction (0-7, 8=centered). |
| `setDpad(dir)` | Alias for `setHat()`. |
| `getHat()` | Returns current hat value. |

### Callbacks

```cpp
ESP32XInput.onRumble([](uint8_t leftMotor, uint8_t rightMotor) {
    // Called when host sends rumble command
});

ESP32XInput.onLed([](uint8_t ledIndex) {
    // Called when host changes LED
});
```

## Compiling

The FQBN **must** include `USBMode=default` to enable USB-OTG (TinyUSB) mode. For UART upload when USB-OTG is active, also add `UploadMode=default`.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default,UploadMode=default" <sketch>
```

## USB Descriptor Layout

```
Configuration Descriptor (37 bytes)
├─ Config header (9 bytes)
├─ Interface (9 bytes) — class 0xFF, subclass 0x5D, protocol 0x01
├─ CS_INTERFACE (5 bytes) — type 0x24, payload 0x00 0x5D 0x01
├─ Endpoint IN 0x81 (7 bytes) — interrupt, 32 bytes, 4ms
└─ Endpoint OUT 0x01 (7 bytes) — interrupt, 32 bytes, 8ms
```

## Report Format

20-byte packed `XInputReport`:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 | `wButtons` | Bitmask of 15 buttons |
| 2 | 1 | `bLeftTrigger` | Left analog trigger (0-255) |
| 3 | 1 | `bRightTrigger` | Right analog trigger (0-255) |
| 4 | 2 | `sThumbLX` | Left stick X (-32767..32767) |
| 6 | 2 | `sThumbLY` | Left stick Y (-32767..32767) |
| 8 | 2 | `sThumbRX` | Right stick X (-32767..32767) |
| 10 | 2 | `sThumbRY` | Right stick Y (-32767..32767) |
| 12 | 4 | `dwReserved0` | Padding |
| 16 | 4 | `dwReserved1` | Padding |

## Performance

| Event Type | Median | P95 | Min |
|-----------|--------|-----|-----|
| Left stick | 4.4ms | 7.7ms | 22us |
| Right stick | 4.3ms | 7.7ms | 159us |
| Left trigger | 4.7ms | 8.1ms | 54us |
| Right trigger | 4.8ms | 8.1ms | 8us |
| Buttons | ~30ms | ~43ms | 16ms |

## Examples

- **BasicButtons** — toggles the A button every 500ms
- **BasicGamepad** — UART command parser for automated testing
- **JoystickTest** — sweeps both sticks and triggers through their full range
- **FullController** — GPIO buttons + analog sticks
- **RumbleFeedback** — registers rumble/LED callbacks and cycles face buttons

## Testing

```bash
# On-board unit tests
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default" \
  --library /path/to/ESP32XInput tests/TestBasicFunctionality

# Latency benchmark
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default" \
  --library /path/to/ESP32XInput tests/LatencyBenchmark

# E2E latency test
sudo python3 scripts/run_tests.py --uart /dev/ttyUSB0
```

## License

MIT
