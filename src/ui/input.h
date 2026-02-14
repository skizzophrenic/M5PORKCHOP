// Input abstraction for touch/button-only devices (e.g. M5Stack Core2).
// Keeps the rest of the codebase off raw M5Unified button/touch details.
#pragma once

#include <stdint.h>

namespace Input {

struct TapEvent {
    int16_t x = -1;   // Screen coordinates
    int16_t y = -1;   // Screen coordinates
};

void init();
void update();

// Button events (edge-triggered; true once per update).
bool up();
bool down();
bool select();

// Special actions (edge-triggered).
bool back();        // BtnB hold
bool screenshot();  // BtnC hold

// Touch gestures (edge-triggered; consume-on-read).
bool swipeLeft();
bool swipeRight();
bool tap(TapEvent& out);

}  // namespace Input

