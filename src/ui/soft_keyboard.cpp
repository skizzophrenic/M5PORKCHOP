// Touchscreen soft keyboard for Core2 (320x240).

#include "soft_keyboard.h"

#include "display.h"
#include "haptic.h"
#include "input.h"
#include "../audio/sfx.h"

#include <ctype.h>
#include <string.h>

bool SoftKeyboard::active = false;
bool SoftKeyboard::done = false;
bool SoftKeyboard::accepted = false;
bool SoftKeyboard::shift = false;
bool SoftKeyboard::masked = false;
const char* SoftKeyboard::title = nullptr;
char* SoftKeyboard::targetBuf = nullptr;
size_t SoftKeyboard::targetCap = 0;
size_t SoftKeyboard::maxLen = 0;

static size_t safeStrlen(const char* s, size_t cap) {
    if (!s || cap == 0) return 0;
    size_t n = 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

void SoftKeyboard::start(const char* t, char* buf, size_t bufCap, size_t maxLen_, bool masked_) {
    title = t;
    targetBuf = buf;
    targetCap = bufCap;
    maxLen = maxLen_;
    masked = masked_;
    shift = false;
    done = false;
    accepted = false;
    active = (targetBuf != nullptr && targetCap > 1);
    if (active) {
        // Ensure NUL-terminated.
        targetBuf[targetCap - 1] = '\0';
    }
}

void SoftKeyboard::stop() {
    active = false;
    done = false;
    accepted = false;
    shift = false;
    title = nullptr;
    targetBuf = nullptr;
    targetCap = 0;
    maxLen = 0;
    masked = false;
}

bool SoftKeyboard::isActive() {
    return active;
}

bool SoftKeyboard::consumeDone(bool& outAccepted) {
    if (!done) return false;
    outAccepted = accepted;
    done = false;  // one-shot
    active = false;
    return true;
}

void SoftKeyboard::appendChar(char c) {
    if (!targetBuf || targetCap == 0) return;
    size_t len = safeStrlen(targetBuf, targetCap - 1);
    if (len >= maxLen) return;
    if (len + 1 >= targetCap) return;
    targetBuf[len] = c;
    targetBuf[len + 1] = '\0';
}

void SoftKeyboard::backspace() {
    if (!targetBuf || targetCap == 0) return;
    size_t len = safeStrlen(targetBuf, targetCap - 1);
    if (len == 0) return;
    targetBuf[len - 1] = '\0';
}

void SoftKeyboard::clear() {
    if (!targetBuf || targetCap == 0) return;
    targetBuf[0] = '\0';
}

static bool pointInRect(int16_t px, int16_t py, int x, int y, int w, int h) {
    return (px >= x && px < (x + w) && py >= y && py < (y + h));
}

void SoftKeyboard::update() {
    if (!active) return;

    // Allow global back-hold to cancel cleanly.
    if (Input::back()) {
        accepted = false;
        done = true;
        return;
    }

    Input::TapEvent tap;
    if (!Input::tap(tap)) return;

    // Only handle taps inside main canvas region.
    if (tap.y < TOP_BAR_H || tap.y >= (TOP_BAR_H + MAIN_H)) return;

    int16_t x = tap.x;
    int16_t y = (int16_t)(tap.y - TOP_BAR_H);

    // Layout constants
    const int pad = 6;
    const int headerH = 40;
    const int keyAreaY = headerH;
    const int keyAreaH = MAIN_H - headerH;
    if (keyAreaH <= 0) return;

    const int rowGap = 3;
    const int colGap = 3;
    const int rows = 5;
    const int rowH = (keyAreaH - (rowGap * (rows - 1))) / rows;
    if (rowH < 20) return;

    // Uniform key width from 10-key row
    const int keyW = (DISPLAY_W - 2 * pad - colGap * 9) / 10;

    const char* row0 = "1234567890";
    const char* row1 = "QWERTYUIOP";
    const char* row2 = "ASDFGHJKL";
    const char* row3 = "ZXCVBNM_";

    auto handleCharRow = [&](const char* letters, int rowIdx, bool applyShift) -> bool {
        const int len = (int)strlen(letters);
        if (len <= 0) return false;
        const int y0 = keyAreaY + rowIdx * (rowH + rowGap);
        const int totalW = len * keyW + (len - 1) * colGap;
        const int leftPad = (DISPLAY_W - totalW) / 2;
        for (int i = 0; i < len; i++) {
            int kx = leftPad + i * (keyW + colGap);
            if (pointInRect(x, y, kx, y0, keyW, rowH)) {
                char c = letters[i];
                if (applyShift && !shift) c = (char)tolower((unsigned char)c);
                appendChar(c);
                return true;
            }
        }
        return false;
    };

    if (handleCharRow(row0, 0, false)) return;  // numbers — no shift
    if (handleCharRow(row1, 1, true)) return;
    if (handleCharRow(row2, 2, true)) return;
    if (handleCharRow(row3, 3, true)) return;

    // Command row: SHIFT | BKSP | SPACE | OK | CANCEL
    const int cmdRowIdx = 4;
    const int y0 = keyAreaY + cmdRowIdx * (rowH + rowGap);
    struct CmdKey { const char* label; int w; };
    CmdKey keys[] = {
        {"SHIFT",  2},
        {"BKSP",   2},
        {"SPACE",  4},
        {"OK",     2},
        {"CANCEL", 2},
    };

    int units = 0;
    for (auto& k : keys) units += k.w;
    const int unitW = (DISPLAY_W - (pad * 2) - (colGap * ((int)(sizeof(keys)/sizeof(keys[0])) - 1))) / units;
    int curX = pad;
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        int kw = keys[i].w * unitW;
        if (pointInRect(x, y, curX, y0, kw, rowH)) {
            if (strcmp(keys[i].label, "SHIFT") == 0) {
                shift = !shift;
                Haptic::play(Haptic::SNAP);
            } else if (strcmp(keys[i].label, "BKSP") == 0) {
                backspace();
            } else if (strcmp(keys[i].label, "SPACE") == 0) {
                appendChar(' ');
            } else if (strcmp(keys[i].label, "OK") == 0) {
                accepted = true;
                done = true;
                Haptic::play(Haptic::THUMP);
                SFX::play(SFX::CLICK);
            } else if (strcmp(keys[i].label, "CANCEL") == 0) {
                accepted = false;
                done = true;
            }
            return;
        }
        curX += kw + colGap;
    }
}

void SoftKeyboard::draw(M5Canvas& canvas) {
    if (!active) return;

    const uint16_t fg = getColorFG();
    const uint16_t bg = getColorBG();

    canvas.fillSprite(bg);
    canvas.setTextColor(fg);
    canvas.setFont(&fonts::Font0);

    // Header — title
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    if (title && title[0]) {
        canvas.drawString(title, 6, 2);
    } else {
        canvas.drawString("INPUT", 6, 2);
    }

    // Input value preview (size 2)
    canvas.setTextSize(2);
    char viewBuf[28];
    viewBuf[0] = '\0';
    if (targetBuf) {
        if (masked) {
            size_t len = safeStrlen(targetBuf, targetCap - 1);
            size_t show = len;
            if (show > sizeof(viewBuf) - 1) show = sizeof(viewBuf) - 1;
            memset(viewBuf, '*', show);
            viewBuf[show] = '\0';
        } else {
            size_t len = safeStrlen(targetBuf, targetCap - 1);
            const char* src = targetBuf;
            if (len > 24) src = targetBuf + (len - 24);
            strncpy(viewBuf, src, sizeof(viewBuf) - 1);
            viewBuf[sizeof(viewBuf) - 1] = '\0';
        }
    }
    canvas.drawRect(4, 20, 312, 20, fg);
    canvas.setTextDatum(top_left);
    canvas.drawString(viewBuf, 8, 22);

    // Key area
    const int pad = 6;
    const int headerH = 40;
    const int keyAreaY = headerH;
    const int keyAreaH = MAIN_H - headerH;
    const int rowGap = 3;
    const int colGap = 3;
    const int rows = 5;
    const int rowH = (keyAreaH - (rowGap * (rows - 1))) / rows;

    // Uniform key width from 10-key row
    const int keyW = (DISPLAY_W - 2 * pad - colGap * 9) / 10;

    auto drawCharRow = [&](const char* letters, int rowIdx, bool applyShift) {
        const int len = (int)strlen(letters);
        const int y0 = keyAreaY + rowIdx * (rowH + rowGap);
        const int totalW = len * keyW + (len - 1) * colGap;
        const int leftPad = (DISPLAY_W - totalW) / 2;
        canvas.setTextSize(2);
        canvas.setTextDatum(middle_center);
        for (int i = 0; i < len; i++) {
            int x0 = leftPad + i * (keyW + colGap);
            canvas.drawRect(x0, y0, keyW, rowH, fg);
            char c = letters[i];
            if (applyShift && !shift) c = (char)tolower((unsigned char)c);
            char s[2] = {c, 0};
            canvas.drawString(s, x0 + keyW / 2, y0 + rowH / 2);
        }
    };

    drawCharRow("1234567890", 0, false);  // numbers — no shift
    drawCharRow("QWERTYUIOP", 1, true);
    drawCharRow("ASDFGHJKL", 2, true);
    drawCharRow("ZXCVBNM_", 3, true);

    // Command row
    const int y0 = keyAreaY + 4 * (rowH + rowGap);
    struct CmdKey { const char* label; int w; };
    CmdKey keys[] = {
        {"SHIFT",  2},
        {"BKSP",   2},
        {"SPACE",  4},
        {"OK",     2},
        {"CANCEL", 2},
    };
    int units = 0;
    for (auto& k : keys) units += k.w;
    const int unitW = (DISPLAY_W - (pad * 2) - (colGap * ((int)(sizeof(keys)/sizeof(keys[0])) - 1))) / units;
    int curX = pad;
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_center);
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        int kw = keys[i].w * unitW;
        uint16_t fill = bg;
        uint16_t text = fg;
        if (strcmp(keys[i].label, "SHIFT") == 0 && shift) {
            fill = fg;
            text = bg;
        }
        canvas.fillRect(curX, y0, kw, rowH, fill);
        canvas.drawRect(curX, y0, kw, rowH, fg);
        canvas.setTextColor(text, fill);
        canvas.drawString(keys[i].label, curX + kw / 2, y0 + rowH / 2);
        canvas.setTextColor(fg, bg);
        curX += kw + colGap;
    }
}

