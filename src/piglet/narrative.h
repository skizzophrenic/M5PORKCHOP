#pragma once
#include <stdint.h>

// Algorithmic narrative engine — generates D&D/Baldur's Gate style
// event log text from combinatorial templates. Monty Python, Bard's Tale,
// and porkchop lore mixed in. Produces millions of unique lines from ~6KB flash.

// Event types for forced narrative lines
enum NarrativeEvent : uint8_t {
    EVT_NONE = 0,
    EVT_HANDSHAKE,       // Handshake captured
    EVT_PMKID,           // PMKID captured
    EVT_DEAUTH_CRIT,     // D20 nat 20 on deauth
    EVT_DEAUTH_FUMBLE,   // D20 nat 1
    EVT_LOW_BATTERY,     // Battery < 15%
    EVT_GPS_LOCK,        // GPS fix acquired
};

class NarrativeEngine {
public:
    // Call every frame; internally rate-limited to ~9s intervals.
    // mode: PorkchopMode enum value (0=IDLE, 1=OINK, 2=DNH, 3=WARHOG, etc.)
    static void update(uint8_t mode);

    static const char* getLine1();   // Newest narrative line
    static const char* getLine2();   // Previous narrative line
    static const char* getLine3();   // Oldest narrative line (3rd scrollback)
    static bool hasContent();
    static bool isFlashing();        // True for 500ms after new line generated

    // Event-driven forced lines (bypass 9s timer, newest wins)
    static void pushEvent(NarrativeEvent evt);

    // D20 roll accumulator — batches rapid rolls into single narrative line
    static void pushD20Roll(uint8_t roll, uint16_t xp);
};
