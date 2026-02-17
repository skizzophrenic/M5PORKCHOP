// Input abstraction for M5Stack Core2 (TouchA/B/C + touchscreen).
// Designed to avoid hot-path heap use and to be deterministic.

#include "input.h"
#include "haptic.h"

#include <M5Unified.h>

namespace Input {

// --- Button events (latched per-frame) ---
static bool evUp = false;
static bool evDown = false;
static bool evSelect = false;
static bool evBack = false;
static bool evScreenshot = false;
static bool evDoubleClick = false;
static bool evPowerShort = false;
static bool evNarratorTap = false;

// Hold detection (Core2 touch buttons are still exposed as Button_Class)
static bool backFired = false;
static bool shotFired = false;
static uint32_t bPressStartMs = 0;
static uint32_t cPressStartMs = 0;

static constexpr uint32_t kBackHoldMs = 700;
static constexpr uint32_t kShotHoldMs = 1100;

// --- Touch gesture state ---
static bool evSwipeLeft = false;
static bool evSwipeRight = false;
static bool evSwipeUp = false;
static bool evSwipeDown = false;
static bool evTap = false;
static TapEvent tapEv = {};

static bool touchDown = false;
static int16_t touchStartX = 0;
static int16_t touchStartY = 0;
static uint32_t touchStartMs = 0;

static constexpr int16_t kTapMovePx = 10;
static constexpr uint32_t kTapMaxMs = 350;
static constexpr int16_t kSwipeMinPx = 60;
static constexpr uint32_t kSwipeMaxMs = 900;

// On-screen nav button zones (bottom bar area, y >= 220)
static constexpr int16_t kNavBarY = 220;   // DISPLAY_H - BOTTOM_BAR_H
static constexpr int16_t kNavBtnW = 107;   // DISPLAY_W / 3

void init() {
    // No-op for now (kept for symmetry / future expansion).
}

static void clearFrameEvents() {
    evUp = evDown = evSelect = evBack = evScreenshot = evDoubleClick = evPowerShort = false;
    evSwipeLeft = evSwipeRight = evSwipeUp = evSwipeDown = false;
    evTap = false;
    evNarratorTap = false;
    tapEv = {};
}

void update() {
    clearFrameEvents();

    // Buttons
    // Use click events so long-holds (back/screenshot) don't also trigger Up/Down/Select.
    evUp = M5.BtnA.wasClicked();
    evDown = M5.BtnC.wasClicked();
    evSelect = M5.BtnB.wasClicked();
    evDoubleClick = M5.BtnB.wasDoubleClicked();

    // Power button short press (AXP192)
    uint8_t pwrKey = M5.Power.getKeyState();
    if (pwrKey == 1) evPowerShort = true;

    // Back hold (BtnB)
    if (M5.BtnB.isPressed()) {
        if (bPressStartMs == 0) bPressStartMs = millis();
        if (!backFired && (millis() - bPressStartMs) >= kBackHoldMs) {
            evBack = true;
            backFired = true;
        }
    } else {
        bPressStartMs = 0;
        backFired = false;
    }

    // Screenshot hold (BtnC)
    if (M5.BtnC.isPressed()) {
        if (cPressStartMs == 0) cPressStartMs = millis();
        if (!shotFired && (millis() - cPressStartMs) >= kShotHoldMs) {
            evScreenshot = true;
            shotFired = true;
        }
    } else {
        cPressStartMs = 0;
        shotFired = false;
    }

    // Touch gestures
    auto t = M5.Touch.getDetail();

    if (t.wasPressed()) {
        touchDown = true;
        touchStartX = t.x;
        touchStartY = t.y;
        touchStartMs = millis();
    }

    if (touchDown && t.wasReleased()) {
        touchDown = false;
        const uint32_t dt = millis() - touchStartMs;
        const int16_t dx = (int16_t)(t.x - touchStartX);
        const int16_t dy = (int16_t)(t.y - touchStartY);

        const int16_t adx = dx < 0 ? (int16_t)-dx : dx;
        const int16_t ady = dy < 0 ? (int16_t)-dy : dy;

        if (dt <= kTapMaxMs && adx <= kTapMovePx && ady <= kTapMovePx) {
            // Taps in the bottom bar area → on-screen nav buttons
            if (t.y >= kNavBarY) {
                if (t.x < kNavBtnW) evUp = true;
                else if (t.x >= kNavBtnW * 2) evDown = true;
                else evNarratorTap = true;  // Center-bottom → narrator toggle
            } else {
                evTap = true;
                tapEv.x = t.x;
                tapEv.y = t.y;
            }
        } else if (dt <= kSwipeMaxMs && adx >= kSwipeMinPx && adx > (ady * 2)) {
            if (dx < 0) evSwipeLeft = true;
            else evSwipeRight = true;
        } else if (dt <= kSwipeMaxMs && ady >= kSwipeMinPx && ady > (adx * 2)) {
            if (dy < 0) evSwipeUp = true;
            else evSwipeDown = true;
        }
    }

    // Haptic tick for any input event (centralized — no per-file tick calls needed)
    if (evUp || evDown || evSelect || evBack || evScreenshot || evDoubleClick ||
        evSwipeLeft || evSwipeRight || evSwipeUp || evSwipeDown || evTap || evNarratorTap)
        Haptic::tick();
}

bool up() { bool v = evUp; evUp = false; return v; }
bool down() { bool v = evDown; evDown = false; return v; }
bool select() { bool v = evSelect; evSelect = false; return v; }
bool back() { bool v = evBack; evBack = false; return v; }
bool screenshot() { bool v = evScreenshot; evScreenshot = false; return v; }
bool doubleClick() { bool v = evDoubleClick; evDoubleClick = false; return v; }
bool powerShort() { bool v = evPowerShort; evPowerShort = false; return v; }
bool swipeLeft() { bool v = evSwipeLeft; evSwipeLeft = false; return v; }
bool swipeRight() { bool v = evSwipeRight; evSwipeRight = false; return v; }
bool swipeUp() { bool v = evSwipeUp; evSwipeUp = false; return v; }
bool swipeDown() { bool v = evSwipeDown; evSwipeDown = false; return v; }
bool tap(TapEvent& out) {
    if (!evTap) return false;
    out = tapEv;
    evTap = false;
    tapEv = {};
    return true;
}

bool narratorTap() { bool v = evNarratorTap; evNarratorTap = false; return v; }

}  // namespace Input
