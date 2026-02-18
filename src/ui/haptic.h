// Haptic feedback for M5Stack Core2 vibration motor
// Pattern sequencer + LED sync. Timer-based, non-blocking.
#pragma once

#include <stdint.h>

namespace Haptic {

// ==[ PATTERNS ]== named haptic sequences
enum Pattern {
    TICK,           // 20ms @ 128  — UI click, scroll
    SNAP,           // 30ms @ 200  — toggle, channel lock
    THUMP,          // 60ms @ 220  — deauth kick, confirm
    PULSE,          // 80ms @ 180  — achievement, sync
    DOUBLE_TAP,     // 25ms on, 40ms off, 25ms on — PMKID, alert
    REWARD,         // 150ms on, 50ms off, 200ms on — level up, streak (400ms total)
    EPIC,           // 100/120/150ms crescendo — ultra streak, sweep (450ms total)
    ERROR_BUZZ,     // 2x 60ms @ 220 — error (distinct from success)
    BOOT_RUMBLE,    // 400ms @ 128 — boot "waking up"
    NOTIFY,         // 100ms on, 60ms off, 80ms on — incoming call, sync
    DEATH,          // 200ms strong, 200ms medium, 400ms fade — death sequence
};

void init();
void update();          // Call from main loop to auto-stop motor/LED

void play(Pattern p);   // Start a haptic pattern (non-blocking)
bool isPlaying();        // Is a pattern currently running?
void stop();             // Cancel current pattern

// ==[ CONVENIENCE ]== kept for backward compat
inline void tick()  { play(TICK); }
inline void pulse() { play(PULSE); }
inline void buzz()  { play(THUMP); }

}  // namespace Haptic
