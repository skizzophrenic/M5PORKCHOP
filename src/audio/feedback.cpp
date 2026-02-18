/**
 * Feedback - Unified Audio + Haptic Trigger
 *
 * Centralized congruency map: each SFX event paired with the right haptic.
 * Haptic fires first (immediate), audio queues (ring buffer).
 * ERM motor 50-100ms rise time creates natural haptic-before-audio feel.
 *
 * Research basis:
 * - 400ms haptic = peak reward (Hampton & Hildebrand, 2025)
 * - Audio-haptic superadditivity within 100-200ms window (Stein & Stanford, 2008)
 * - Low pitch ↔ strong haptic, ascending ↔ crescendo (crossmodal congruency)
 */

#include "feedback.h"
#include "../ui/haptic.h"

namespace Feedback {

void play(SFX::Event event) {
    // Fire audio (queued, non-blocking)
    SFX::play(event);

    // Fire haptic (immediate) — congruency map
    switch (event) {

        // === OINK MODE ===
        case SFX::DEAUTH:
            Haptic::play(Haptic::THUMP);          // Low kick = heavy haptic
            break;
        case SFX::HANDSHAKE:
            Haptic::play(Haptic::REWARD);          // Big capture = 400ms reward
            break;
        case SFX::PMKID:
            Haptic::play(Haptic::DOUBLE_TAP);      // Quick "found it" double-tap
            break;
        case SFX::NETWORK_NEW:
            Haptic::play(Haptic::TICK);            // Subtle — fires often
            break;

        // === SPECTRUM MODE ===
        case SFX::CLIENT_FOUND:
            Haptic::play(Haptic::TICK);            // Subtle discovery
            break;
        case SFX::SIGNAL_LOST:
            Haptic::play(Haptic::SNAP);            // Brief "gone" snap
            break;
        case SFX::CHANNEL_LOCK:
            Haptic::play(Haptic::SNAP);            // Confirmation snap
            break;
        case SFX::REVEAL_START:
            Haptic::play(Haptic::TICK);            // Soft mode-change tick
            break;

        // === GAMIFICATION ===
        case SFX::ACHIEVEMENT:
            Haptic::play(Haptic::PULSE);           // Earned — moderate celebration
            break;
        case SFX::LEVEL_UP:
            Haptic::play(Haptic::REWARD);          // 400ms reward sweet spot
            break;
        case SFX::JACKPOT_XP:
            Haptic::play(Haptic::PULSE);           // Exciting but brief
            break;
        case SFX::ULTRA_STREAK:
            Haptic::play(Haptic::EPIC);            // Crescendo — peak moment
            break;
        case SFX::CHALLENGE_COMPLETE:
            Haptic::play(Haptic::PULSE);           // Nice work
            break;
        case SFX::CHALLENGE_SWEEP:
            Haptic::play(Haptic::EPIC);            // Legendary — full crescendo
            break;

        // === BLE SYNC ===
        case SFX::CALL_RING:
            Haptic::play(Haptic::NOTIFY);          // Incoming attention
            break;
        case SFX::SYNC_COMPLETE:
            Haptic::play(Haptic::PULSE);           // Done
            break;

        // === SYSTEM ===
        case SFX::ERROR:
            Haptic::play(Haptic::ERROR_BUZZ);      // Distinct double-buzz
            break;
        case SFX::BOOT:
            Haptic::play(Haptic::BOOT_RUMBLE);     // Waking up
            break;

        // === SPECIAL ===
        case SFX::YOU_DIED:
            Haptic::play(Haptic::DEATH);           // Descending fade-out
            break;

        // === NO HAPTIC (intentional) ===
        // CLICK / MENU_CLICK: handled by Input::update() → Haptic::tick()
        // TERMINAL_TICK: ambient, no physical feedback
        // PIGSYNC_BOOT: terminal animation, no haptic
        // SIREN: visual sync only
        case SFX::CLICK:
        case SFX::MENU_CLICK:
        case SFX::TERMINAL_TICK:
        case SFX::PIGSYNC_BOOT:
        case SFX::SIREN:
        default:
            break;
    }
}

}  // namespace Feedback
