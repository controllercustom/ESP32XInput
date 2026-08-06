// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com

// AutoCycle — phased, deterministic test for pcap analysis and verification.
// 9 phases P0-P8 with phase markers between transitions, callback telemetry,
// disabled auto-poll timer, explicit send() per iteration only.

#include <ESP32XInput.h>

static const uint16_t BTN_NAMES[] = {
    ESP32XInputClass::DPAD_UP,   // 0 (also setHat handles this)
};

#define PHASE_MARKER_DELAY_MS 8U
#define ITERATION_DELAY_MS    10U

// --- Phase marker: unmistakable all-buttons frame for pcap boundary detection.
static void sendPhaseMarker() {
    ESP32XInput.releaseAll();
    delay(ITERATION_DELAY_MS);

    // Press every button (excluding d-pad bits which are set by setHat).
    for (uint8_t i = 4; i < ESP32XInputClass::BUTTON_COUNT; ++i) {
        ESP32XInput.press(static_cast<ESP32XInputClass::Button>(i));
    }
    // Also press d-pad buttons via setHat(0) — UP.
    ESP32XInput.setHat(0);

    Serial0.println("MARKER:ALL_BUTTONS");
    ESP32XInput.send();
    delay(PHASE_MARKER_DELAY_MS);

    ESP32XInput.releaseAll();
    ESP32XInput.send();
}

// --- Button table for P1 (all 15 buttons, excluding d-pad which is in wButtons bits 0-3).
static const struct { uint8_t idx; const char* name; } buttonTable[] = {
    // D-pad buttons handled via setHat() — not individual press/release.
    {ESP32XInputClass::START,       "START"},
    {ESP32XInputClass::BACK,        "BACK"},
    {ESP32XInputClass::LEFT_THUMB,  "LTHUMB"},
    {ESP32XInputClass::RIGHT_THUMB, "RTHUMB"},
    {ESP32XInputClass::LEFT_SHOULDER,  "LB"},
    {ESP32XInputClass::RIGHT_SHOULDER, "RB"},
    {ESP32XInputClass::XBOX,        "XBOX"},
    {ESP32XInputClass::A,           "A"},
    {ESP32XInputClass::B,           "B"},
    {ESP32XInputClass::X,           "X"},
    {ESP32XInputClass::Y,           "Y"},
};

#define BTN_TABLE_SIZE (sizeof(buttonTable) / sizeof(buttonTable[0]))

// --- Stick sweep tables for P3/P4.
static const struct { int16_t x; int16_t y; } stickLinearSweep[] = {
    {-29491,  0     }, // ~-90% X
    {-19660,  0     }, // ~-60% X
    {-9830 ,  0     }, // ~-30% X
    {      0,  0     }, // center
    {+9830 ,  0     }, // +30% X
    {+19660,  0     }, // +60% X
    {+29491,  0     }, // +90% X

    {      0,-29491}, // ~-90% Y
    {      0,-19660}, // ~-60% Y
    {      0,-9830 }, // ~-30% Y
    {      0,     0}, // center
    {      0,+9830 }, // +30% Y
    {      0,+19660}, // +60% Y
    {      0,+29491}, // +90% Y

    {     -32768,   -32768}, // bottom-left extreme (note: XInput uses signed range)
};

#define STICK_LINEAR_SIZE (sizeof(stickLinearSweep) / sizeof(stickLinearSweep[0]))

static const struct { int16_t x; int16_t y; } stickCircle[] = {
    {-9830, -9830}, // ~50% radius: bottom-left quadrant → actually top-left in XInput coords (Y inverted)
    {+9830, -9830}, // top-right
    {+9830, +9830}, // bottom-right
    {-9830, +9830}, // bottom-left

    {-14745,-14745}, // ~75% radius: same quadrants
    {+14745,-14745},
    {+14745,+14745},
    {-14745,+14745},

    {0, 0}, // return to center
};

#define STICK_CIRCLE_SIZE (sizeof(stickCircle) / sizeof(stickCircle[0]))

// --- Trigger ramp table for P5/P6.
static const uint16_t triggerRamp[] = {
    0, 4096, 8192, 16384, 24576, 32768, 16384, 0
};

#define TRIGGER_RAMP_SIZE (sizeof(triggerRamp) / sizeof(triggerRamp[0]))

// --- D-pad sweep table for P2.
static const uint8_t dpadSweep[] = {
    0, // UP
    1, // UP_RIGHT
    2, // RIGHT
    3, // DOWN_RIGHT
    4, // DOWN
    5, // DOWN_LEFT
    6, // LEFT
    7, // UP_LEFT
    8, // CENTERED (hold)
    8, // CENTERED (confirm clean state)
};

#define DPAD_SWEEP_SIZE (sizeof(dpadSweep) / sizeof(dpadSweep[0]))

// --- Phase iteration counts.
static const uint16_t PHASE_IDLE_ITERS = 50;   // P0 and P8: ~5s each at 10ms/iter
#define BTN_PASSES     3                        // P1: full passes over button table (press×2, release×1 per btn)

void setup() {
    Serial0.begin(115200);
    delay(200);

    ESP32XInput.setPollInterval(0);

    ESP32XInput.onRumble([](uint8_t lMotor, uint8_t rMotor) {
        Serial0.printf("CB_RUMBLE:left=%u,right=%u\n", lMotor, rMotor);
    });
    ESP32XInput.onLed([](uint8_t idx) {
        Serial0.printf("CB_LED:%u\n", idx);
    });

    ESP32XInput.begin();

    unsigned long start = millis();
    while (!ESP32XInput.isConnected() && (millis() - start) < 5000UL) {
        delay(10);
    }
    Serial0.println("READY");
}

void loop() {
    static uint8_t phase = 0;
    static uint16_t seqInPhase = 0;
    static uint32_t globalSeq = 0;

    // --- P0: Idle baseline (~5s). Clean zero-state capture window.
    if (phase == 0) {
        ESP32XInput.releaseAll();
        Serial0.printf("PHASE=0 SEQ=%u TS:%lld\n", seqInPhase, esp_timer_get_time());

        if (++seqInPhase >= PHASE_IDLE_ITERS) {
            sendPhaseMarker();
            phase = 1;
            seqInPhase = 0;
        }
    }

    // --- P1: Individual button cycle. Each btn pressed for 2 iters, released for 1. Multiple passes.
    else if (phase == 1) {
        ESP32XInput.releaseAll();

        uint8_t pass = seqInPhase / BTN_TABLE_SIZE;
        uint8_t btnIdx = seqInPhase % BTN_TABLE_SIZE;
        bool pressHold = (seqInPhase % 3 != 0); // Press on iter N, hold on N+1, release on N+2.

        if (pressHold) {
            ESP32XInput.press(static_cast<ESP32XInputClass::Button>(buttonTable[btnIdx].idx));
            Serial0.printf("PHASE=1 SEQ=%u PASS=%d BTN=%s TS:%lld\n", seqInPhase, pass + 1, buttonTable[btnIdx].name, esp_timer_get_time());
        } else {
            ESP32XInput.release(static_cast<ESP32XInputClass::Button>(buttonTable[btnIdx].idx));
            Serial0.printf("PHASE=1 SEQ=%u PASS=%d BTN=%s(REL) TS:%lld\n", seqInPhase, pass + 1, buttonTable[btnIdx].name, esp_timer_get_time());
        }

        if (++seqInPhase >= BTN_TABLE_SIZE * BTN_PASSES) {
            sendPhaseMarker();
            phase = 2;
            seqInPhase = 0;
        }
    }

    // --- P2: D-pad cardinal + diagonal sweep.
    else if (phase == 2) {
        ESP32XInput.releaseAll();
        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.setStickRight(0, 0);

        uint8_t dpadDir = dpadSweep[seqInPhase];
        ESP32XInput.setHat(dpadDir);

        Serial0.printf("PHASE=2 SEQ=%u DPAD=%d TS:%lld\n", seqInPhase, dpadDir, esp_timer_get_time());

        if (++seqInPhase >= DPAD_SWEEP_SIZE) {
            sendPhaseMarker();
            phase = 3;
            seqInPhase = 0;
        }
    }

    // --- P3: Left stick linear sweep + circle. Right stick centered throughout.
    else if (phase == 3) {
        ESP32XInput.releaseAll();

        int16_t sx, sy;
        const char* label = "LINEAR";
        uint8_t stepIdx = seqInPhase % STICK_LINEAR_SIZE;

        if (seqInPhase < STICK_LINEAR_SIZE) {
            // Linear sweep.
            sx = stickLinearSweep[stepIdx].x;
            sy = stickLinearSweep[stepIdx].y;
        } else {
            // Circle sub-sequence.
            label = "CIRCLE";
            stepIdx = seqInPhase - STICK_LINEAR_SIZE;
            if (stepIdx < STICK_CIRCLE_SIZE) {
                sx = stickCircle[stepIdx].x;
                sy = stickCircle[stepIdx].y;
            } else {
                // Past circle — return to center.
                sx = 0;
                sy = 0;
            }
        }

        ESP32XInput.setStickLeft(sx, sy);
        ESP32XInput.setStickRight(0, 0);

        Serial0.printf("PHASE=3 SEQ=%u %s LX=(%d,%d) TS:%lld\n", seqInPhase, label, sx, sy, esp_timer_get_time());

        uint16_t totalIters = STICK_LINEAR_SIZE + STICK_CIRCLE_SIZE;
        if (++seqInPhase >= totalIters) {
            sendPhaseMarker();
            phase = 4;
            seqInPhase = 0;
        }
    }

    // --- P4: Right stick sweep (mirrors P3). Left stick centered throughout.
    else if (phase == 4) {
        ESP32XInput.releaseAll();

        int16_t sx, sy;
        const char* label = "LINEAR";
        uint8_t stepIdx = seqInPhase % STICK_LINEAR_SIZE;

        if (seqInPhase < STICK_LINEAR_SIZE) {
            sx = stickLinearSweep[stepIdx].x;
            sy = stickLinearSweep[stepIdx].y;
        } else {
            label = "CIRCLE";
            stepIdx = seqInPhase - STICK_LINEAR_SIZE;
            if (stepIdx < STICK_CIRCLE_SIZE) {
                sx = stickCircle[stepIdx].x;
                sy = stickCircle[stepIdx].y;
            } else {
                sx = 0;
                sy = 0;
            }
        }

        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.setStickRight(sx, sy);

        Serial0.printf("PHASE=4 SEQ=%u %s RX=(%d,%d) TS:%lld\n", seqInPhase, label, sx, sy, esp_timer_get_time());

        uint16_t totalIters = STICK_LINEAR_SIZE + STICK_CIRCLE_SIZE;
        if (++seqInPhase >= totalIters) {
            sendPhaseMarker();
            phase = 5;
            seqInPhase = 0;
        }
    }

    // --- P5: L-trigger smooth ramp. R-trigger held at 0, sticks centered, no buttons.
    else if (phase == 5) {
        ESP32XInput.releaseAll();
        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.setStickRight(0, 0);

        uint16_t ltVal = triggerRamp[seqInPhase];
        ESP32XInput.setLeftTrigger(ltVal);
        ESP32XInput.setRightTrigger(0);

        Serial0.printf("PHASE=5 SEQ=%u LT=%d TS:%lld\n", seqInPhase, ltVal, esp_timer_get_time());

        if (++seqInPhase >= TRIGGER_RAMP_SIZE) {
            sendPhaseMarker();
            phase = 6;
            seqInPhase = 0;
        }
    }

    // --- P6: R-trigger smooth ramp. L-trigger held at 0.
    else if (phase == 6) {
        ESP32XInput.releaseAll();
        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.setStickRight(0, 0);

        uint16_t rtVal = triggerRamp[seqInPhase];
        ESP32XInput.setLeftTrigger(0);
        ESP32XInput.setRightTrigger(rtVal);

        Serial0.printf("PHASE=6 SEQ=%u RT=%d TS:%lld\n", seqInPhase, rtVal, esp_timer_get_time());

        if (++seqInPhase >= TRIGGER_RAMP_SIZE) {
            sendPhaseMarker();
            phase = 7;
            seqInPhase = 0;
        }
    }

    // --- P7a: All inputs simultaneously — max complexity report encoding.
    else if (phase == 7) {
        ESP32XInput.releaseAll();

        for (uint8_t i = 0; i < BTN_TABLE_SIZE; ++i) {
            ESP32XInput.press(static_cast<ESP32XInputClass::Button>(buttonTable[i].idx));
        }
        ESP32XInput.setHat(0); // UP.
        ESP32XInput.setStickLeft(-16384, -16384);
        ESP32XInput.setStickRight(+16384, +16384);
        ESP32XInput.setLeftTrigger(16384);
        ESP32XInput.setRightTrigger(16384);

        Serial0.printf("PHASE=7a SEQ=%u ALL_INPUTS TS:%lld\n", seqInPhase, esp_timer_get_time());

        if (++seqInPhase >= 5) {
            sendPhaseMarker();
            phase = 8;
            seqInPhase = 0;
        }
    }

    // --- P7b: Rapid-fire toggle of button A. Everything else zeroed.
    else if (phase == 8) {
        ESP32XInput.releaseAll();

        bool pressA = ((seqInPhase % 2) == 1);
        ESP32XInput.setButton(ESP32XInputClass::A, pressA);

        Serial0.printf("PHASE=7b SEQ=%u RAPID_FIRE(A=%d) TS:%lld\n", seqInPhase, pressA ? 1 : 0, esp_timer_get_time());

        if (++seqInPhase >= 25) {
            sendPhaseMarker();
            phase = 9;
            seqInPhase = 0;
        }
    }

    // --- P7c: Trailing idle — clean zero reports to verify recovery after stress.
    else if (phase == 9) {
        ESP32XInput.releaseAll();

        Serial0.printf("PHASE=7c SEQ=%u STRESS_IDLE TS:%lld\n", seqInPhase, esp_timer_get_time());

        if (++seqInPhase >= PHASE_IDLE_ITERS / 4) { // ~1.25s idle after stress
            sendPhaseMarker();
            phase = 10;
            seqInPhase = 0;
        }
    }

    // --- P8: Sustained idle (~5s). Stable capture window at end of cycle.
    else {
        ESP32XInput.releaseAll();
        Serial0.printf("PHASE=8 SEQ=%u TS:%lld\n", seqInPhase, esp_timer_get_time());

        if (++seqInPhase >= PHASE_IDLE_ITERS) {
            // Full cycle complete — restart from P0.
            sendPhaseMarker();
            phase = 0;
            seqInPhase = 0;
            globalSeq = 0;
        }
    }

    ESP32XInput.send();
    ESP32XInput.pollRumble();
    delay(ITERATION_DELAY_MS);
}

