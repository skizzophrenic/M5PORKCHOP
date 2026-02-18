/**
 * Feedback - Unified Audio + Haptic Trigger
 *
 * Fires both SFX and Haptic with correct timing and congruent pairing.
 * Single call replaces scattered SFX::play() + Haptic::xxx() pairs.
 *
 * Haptic fires immediately; audio queues through SFX ring buffer.
 * ERM motor rise time (~50-100ms) naturally creates the 10-50ms lead
 * that research shows feels most natural (Stein & Stanford, 2008).
 */

#ifndef FEEDBACK_H
#define FEEDBACK_H

#include "sfx.h"

namespace Feedback {

// Fire both audio + haptic for an event.
// Haptic pattern is selected automatically via congruency map.
// Safe to call from anywhere (SFX ring buffer is callback-safe).
void play(SFX::Event event);

}  // namespace Feedback

#endif  // FEEDBACK_H
