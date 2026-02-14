// Touchscreen soft keyboard for Core2-style devices.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <M5Unified.h>

class SoftKeyboard {
public:
    static void start(const char* title, char* buf, size_t bufCap, size_t maxLen, bool masked);
    static void stop();
    static bool isActive();

    // Returns true once when finished (accepted or canceled).
    // If accepted, the edited text is already written back to the target buffer.
    static bool consumeDone(bool& accepted);

    static void update();          // Uses M5.Touch + Input-style tap coordinates.
    static void draw(M5Canvas& canvas);

private:
    struct Key {
        const char* label;
        // If ch != 0, key inserts character. Otherwise it's a command key.
        char ch;
        bool isCmd;
    };

    static bool active;
    static bool done;
    static bool accepted;
    static bool shift;
    static bool masked;

    static const char* title;
    static char* targetBuf;
    static size_t targetCap;
    static size_t maxLen;

    static void appendChar(char c);
    static void backspace();
    static void clear();
};

