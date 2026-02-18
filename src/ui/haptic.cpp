// Haptic feedback for M5Stack Core2 vibration motor
// Pattern sequencer with LED sync. Non-blocking, timer-driven.
//
// Uses M5.Power.setVibration(0-255) on AXP192 LDO3.
// Voltage = 480 + level*12 mV. Motor needs ~1500mV (level ~85) to spin.
// ERM rise time ~50-100ms — don't expect crisp sub-30ms pulses.

#include "haptic.h"
#include <M5Unified.h>

namespace Haptic {

// ==[ STEP DEFINITION ]==
// power=0 means gap (motor off). {0,0} terminates sequence.
struct Step {
    uint8_t  power;     // 0-255 (0 = gap between pulses)
    uint16_t duration;  // ms (0 = end of sequence)
};

// ==[ PATTERN SEQUENCES ]==

static const Step PAT_TICK[] = {
    {128, 20},
    {0, 0}
};

static const Step PAT_SNAP[] = {
    {200, 30},
    {0, 0}
};

static const Step PAT_THUMP[] = {
    {220, 60},
    {0, 0}
};

static const Step PAT_PULSE[] = {
    {180, 80},
    {0, 0}
};

// Double-tap: two quick pulses with gap
static const Step PAT_DOUBLE_TAP[] = {
    {180, 25},
    {0, 40},    // gap
    {180, 25},
    {0, 0}
};

// Reward: ~400ms total — the scientifically-backed sweet spot
static const Step PAT_REWARD[] = {
    {160, 150},
    {0, 50},    // gap
    {200, 200},
    {0, 0}
};

// Epic: crescendo pattern ~450ms — for ultra-achievements
static const Step PAT_EPIC[] = {
    {140, 100},
    {0, 40},    // gap
    {180, 120},
    {0, 40},    // gap
    {220, 150},
    {0, 0}
};

// Error: distinct double-buzz — clearly "wrong"
static const Step PAT_ERROR_BUZZ[] = {
    {220, 60},
    {0, 30},    // gap
    {220, 60},
    {0, 0}
};

// Boot: long gentle rumble — "waking up"
static const Step PAT_BOOT_RUMBLE[] = {
    {128, 400},
    {0, 0}
};

// Notify: two-part attention getter
static const Step PAT_NOTIFY[] = {
    {160, 100},
    {0, 60},    // gap
    {140, 80},
    {0, 0}
};

// Death: descending intensity — life draining away
static const Step PAT_DEATH[] = {
    {220, 200},
    {0, 20},
    {180, 200},
    {0, 20},
    {128, 400},
    {0, 0}
};

// ==[ PATTERN LOOKUP ]==
static const Step* const PATTERNS[] = {
    PAT_TICK,         // TICK
    PAT_SNAP,         // SNAP
    PAT_THUMP,        // THUMP
    PAT_PULSE,        // PULSE
    PAT_DOUBLE_TAP,   // DOUBLE_TAP
    PAT_REWARD,       // REWARD
    PAT_EPIC,         // EPIC
    PAT_ERROR_BUZZ,   // ERROR_BUZZ
    PAT_BOOT_RUMBLE,  // BOOT_RUMBLE
    PAT_NOTIFY,       // NOTIFY
    PAT_DEATH,        // DEATH
};

// LED brightness paired with each pattern (0 = no LED)
static const uint8_t PATTERN_LED[] = {
    0,      // TICK — no LED
    0,      // SNAP — no LED
    0,      // THUMP — no LED
    128,    // PULSE — moderate LED
    0,      // DOUBLE_TAP — no LED
    128,    // REWARD — moderate LED
    255,    // EPIC — full LED
    255,    // ERROR_BUZZ — full LED (attention!)
    64,     // BOOT_RUMBLE — dim LED
    128,    // NOTIFY — moderate LED
    255,    // DEATH — full LED
};

// ==[ STATE MACHINE ]==
static const Step* currentPattern = nullptr;
static uint8_t currentStep = 0;
static uint32_t stepStartMs = 0;
static uint32_t deadlineMs = 0;
static bool motorActive = false;
static bool ledActive = false;
static uint8_t currentLedBrightness = 0;

void init() {
    currentPattern = nullptr;
    currentStep = 0;
    motorActive = false;
    ledActive = false;
}

void play(Pattern p) {
    // Stop any current pattern
    if (motorActive) {
        M5.Power.setVibration(0);
        motorActive = false;
    }

    currentPattern = PATTERNS[p];
    currentStep = 0;
    stepStartMs = millis();
    currentLedBrightness = PATTERN_LED[p];

    // Compute hard deadline — sum all step durations
    uint32_t totalDuration = 0;
    for (uint8_t i = 0; currentPattern[i].duration != 0; i++) {
        totalDuration += currentPattern[i].duration;
    }
    deadlineMs = millis() + totalDuration + 50;  // +50ms grace for timer jitter

    // Start first step
    const Step& s = currentPattern[0];
    if (s.duration == 0) {
        currentPattern = nullptr;
        return;
    }
    if (s.power > 0) {
        M5.Power.setVibration(s.power);
        motorActive = true;
    }

    // LED on if this pattern uses LED
    if (currentLedBrightness > 0) {
        M5.Power.setLed(currentLedBrightness);
        ledActive = true;
    }
}

void update() {
    if (currentPattern == nullptr) {
        // Safety: ensure motor/LED are off when idle
        if (motorActive) {
            M5.Power.setVibration(0);
            motorActive = false;
        }
        if (ledActive) {
            M5.Power.setLed(0);
            ledActive = false;
        }
        return;
    }

    // Hard deadline — force-stop if pattern exceeded its total duration
    if (millis() >= deadlineMs) {
        stop();
        return;
    }

    const Step& s = currentPattern[currentStep];

    // Check if current step duration has elapsed
    if (millis() - stepStartMs < s.duration) return;

    // Advance to next step
    currentStep++;
    const Step& next = currentPattern[currentStep];
    stepStartMs = millis();

    // End of sequence?
    if (next.duration == 0) {
        M5.Power.setVibration(0);
        motorActive = false;
        if (ledActive) {
            M5.Power.setLed(0);
            ledActive = false;
        }
        currentPattern = nullptr;
        currentStep = 0;
        return;
    }

    // Apply next step
    if (next.power > 0) {
        M5.Power.setVibration(next.power);
        motorActive = true;
    } else {
        // Gap — motor off but pattern continues
        M5.Power.setVibration(0);
        motorActive = false;
    }
}

bool isPlaying() {
    return currentPattern != nullptr;
}

void stop() {
    if (motorActive) {
        M5.Power.setVibration(0);
        motorActive = false;
    }
    if (ledActive) {
        M5.Power.setLed(0);
        ledActive = false;
    }
    currentPattern = nullptr;
    currentStep = 0;
}

}  // namespace Haptic
