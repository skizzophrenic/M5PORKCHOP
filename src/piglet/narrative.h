#pragma once
#include <stdint.h>

// Algorithmic narrative engine — generates D&D/Baldur's Gate style
// event log text from combinatorial templates. Monty Python, Bard's Tale,
// and porkchop lore mixed in. Produces millions of unique lines from ~6KB flash.

class NarrativeEngine {
public:
    // Call every frame; internally rate-limited to ~9s intervals.
    // mode: PorkchopMode enum value (0=IDLE, 1=OINK, 2=DNH, 3=WARHOG, etc.)
    static void update(uint8_t mode);

    static const char* getLine1();   // Newest narrative line
    static const char* getLine2();   // Previous narrative line
    static bool hasContent();
};
