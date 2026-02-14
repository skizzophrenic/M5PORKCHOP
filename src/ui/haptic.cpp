// Haptic feedback for M5Stack Core2 vibration motor
// Uses M5.Power.setVibration() with timer-based auto-off.

#include "haptic.h"
#include <M5Unified.h>

namespace Haptic {

static uint32_t stopMs = 0;    // millis() when motor should stop (0 = idle)
static bool motorOn = false;

void init() {
    stopMs = 0;
    motorOn = false;
}

void update() {
    if (motorOn && millis() >= stopMs) {
        M5.Power.setVibration(0);
        motorOn = false;
    }
}

static void vibrate(uint8_t power, uint32_t durationMs) {
    M5.Power.setVibration(power);
    stopMs = millis() + durationMs;
    motorOn = true;
}

void tick() {
    vibrate(128, 20);
}

void pulse() {
    vibrate(180, 80);
}

void buzz() {
    vibrate(220, 200);
}

}  // namespace Haptic
