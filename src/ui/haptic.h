// Haptic feedback for M5Stack Core2 vibration motor
#pragma once

#include <stdint.h>

namespace Haptic {

void init();
void update();   // Call from Display::update() to auto-stop motor

void tick();     // 20ms at power 128 (UI click/tap)
void pulse();    // 80ms at power 180 (achievement/level-up)
void buzz();     // 200ms at power 220 (capture/error)

}  // namespace Haptic
