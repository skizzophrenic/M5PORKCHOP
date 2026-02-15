// Haptic feedback for M5Stack Core2 vibration motor
// Uses M5.Power.setVibration() with timer-based auto-off.

#include "haptic.h"
#include <M5Unified.h>

namespace Haptic {

static uint32_t stopMs = 0;    // millis() when motor should stop (0 = idle)
static bool motorOn = false;
static uint32_t ledStopMs = 0;
static bool ledOn = false;

void init() {
    stopMs = 0;
    motorOn = false;
    ledStopMs = 0;
    ledOn = false;
}

void update() {
    if (motorOn && millis() >= stopMs) {
        M5.Power.setVibration(0);
        motorOn = false;
    }
    if (ledOn && millis() >= ledStopMs) {
        M5.Power.setLed(0);
        ledOn = false;
    }
}

static void vibrate(uint8_t power, uint32_t durationMs) {
    M5.Power.setVibration(power);
    stopMs = millis() + durationMs;
    motorOn = true;
}

static void flashLed(uint8_t brightness, uint32_t durationMs) {
    M5.Power.setLed(brightness);
    ledStopMs = millis() + durationMs;
    ledOn = true;
}

void tick() {
    vibrate(128, 20);
}

void pulse() {
    vibrate(180, 80);
    flashLed(128, 80);
}

void buzz() {
    vibrate(220, 200);
    flashLed(255, 200);
}

}  // namespace Haptic
