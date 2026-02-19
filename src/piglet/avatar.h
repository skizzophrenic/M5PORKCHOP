// Piglet ASCII avatar
#pragma once

#include <M5Unified.h>

enum class AvatarState {
    NEUTRAL,
    HAPPY,
    EXCITED,
    HUNTING,
    SLEEPY,
    SAD,
    ANGRY
};

enum class WaveMode : uint8_t {
    NONE,      // No waves (idle, cooldown, bored)
    INCOMING,  // Converging toward nose (scanning/sniffing)
    OUTGOING   // Radiating from nose (deauth/sending)
};

class Avatar {
public:
    static void init();
    static void draw(M5Canvas& canvas);
    static void setState(AvatarState state);
    static AvatarState getState() { return currentState; }
    static bool isFacingRight();  // Get current facing direction
    static bool isOnRightSide();  // Get screen position (for bubble placement)
    static bool isTransitioning();  // True during walk transition (hide bubble)
    static int getCurrentX();  // Get current animated X position

    // Phase 8: Intensity-based animation modifiers
    static void setMoodIntensity(int intensity);  // -100 to 100, affects blink/flip rates

    static void blink();
    static void sniff();  // Trigger nose sniff animation (600ms animated cycle)
    static void wiggleEars();
    static void cuteJump();  // Trigger cute celebratory jump (higher than walk bounce)

    // Direction control
    static void setFacingLeft();
    static void setFacingRight();

    // Attack shake (visual feedback for captures)
    static void setAttackShake(bool active, bool strong);

    // Attack hop (multi-hop pounce animation for captures)
    static void attackHop();
    static bool isAttackHopping();

    // Thunder flash (invert colors for weather effect)
    static void setThunderFlash(bool active);
    static bool isThunderFlashing();

    // Wave ripple animation (radio activity feedback)
    // Burst-based: each call starts a 1500ms burst, re-triggering resets timer
    // OUTGOING priority: active OUTGOING burst can't be overridden by INCOMING
    static void waveRipple(WaveMode mode);
    static WaveMode getWaveMode() { return waveMode; }

    // Night sky star system (RTC-based)
    static bool isNightTime();           // check rtc for night hours, 20:00-06:00
    static bool areStarsActive();        // stars currently visible?

    // Walk wind-up animation (smooth slide for coast-back)
    static void startWindupSlide(int targetX, bool faceRight = false);

    // Grass animation control (direction: true=right, false=left)
    static void setGrassMoving(bool moving, bool directionRight = true);
    static bool isGrassMoving() { return grassMoving; }
    static bool isGrassDirectionRight() { return grassDirection; }
    static uint16_t getGrassSpeed() { return grassSpeed; }
    static void setGrassSpeed(uint16_t ms);  // Speed in ms per shift (lower = faster)
    static void resetGrass();  // Re-randomize blade array

private:
    // Star system state
    struct Star {
        int16_t x;              // screen x range 0-239
        int16_t y;              // screen y range 20-100
        uint8_t size;           // 1-2 px radius
        uint8_t brightness;     // 0-255, 0 means hidden
        bool isBlinking;        // twinkle behavior
        uint32_t fadeInStart;   // when this star started appearing
    };
    static Star stars[15];
    static uint8_t starCount;
    static constexpr uint8_t MAX_STARS = 15;
    static uint32_t lastStarSpawn;
    static uint32_t nextSpawnDelay;
    static bool starsActive;
    static uint32_t lastNightCheck;
    static bool cachedNightMode;

    static void initStarPositions();
    static void updateStars();
    static void drawStars(M5Canvas& canvas);
    static void fillPigBoundingBox(M5Canvas& canvas);
    static AvatarState currentState;
    static bool isBlinking;
    static bool isSniffing;
    static bool earsUp;
    static uint32_t lastBlinkTime;
    static uint32_t blinkInterval;
    static int moodIntensity;  // Phase 8: -100 to 100

    // Cute jump animation state
    static bool jumpActive;
    static uint32_t jumpStartTime;
    static constexpr uint16_t JUMP_DURATION_MS = 400;  // Total jump time (up + down)
    static constexpr int JUMP_HEIGHT = 8;  // Pixels to jump up

    // Attack hop animation state (multi-hop pounce for captures)
    static bool attackHopActive;
    static uint32_t attackHopStartTime;
    static uint8_t attackHopIndex;          // current hop (0-based)
    static uint8_t attackHopTotal;          // total hops (3-5)
    static int16_t attackHopOriginX;        // X before attack started
    static int16_t attackHopTargets[5];     // pre-computed X targets
    static constexpr uint16_t ATTACK_HOP_MS = 250;      // ms per hop
    static constexpr int16_t ATTACK_HOP_HEIGHT = 10;     // pixels up

    // Walk transition animation
    static bool transitioning;
    static uint32_t transitionStartTime;
    static int transitionFromX;
    static int transitionToX;
    static bool transitionToFacingRight;
    static int currentX;  // Animated X position
    static constexpr uint16_t TRANSITION_DURATION_MS = 400;  // Walk across time

    // Grass animation state
    static bool grassMoving;
    static bool grassDirection;  // true = grass scrolls right, false = scrolls left
    static bool pendingGrassStart;  // Wait for transition before starting grass
    static bool onRightSide;  // Track which side of screen pig is on
    static uint32_t lastGrassUpdate;
    static uint16_t grassSpeed;  // ms per shift

    // Grass blade system (triangle primitives)
    struct GrassBlade {
        uint8_t height;  // 6-20 px
        int8_t  lean;    // tip offset: -3 to +3
        uint8_t width;   // base half-width: 1-3
    };
    static constexpr uint8_t GRASS_BLADE_COUNT = 30;
    static constexpr int16_t GRASS_STRIDE = 8;  // px spacing (240/30)
    static GrassBlade grassBlades[GRASS_BLADE_COUNT];
    static int16_t grassOffset;  // smooth scroll pixel offset

    static WaveMode waveMode;
    static uint32_t waveBurstStart;
    static void drawWaveRipples(M5Canvas& canvas, bool faceRight, int startX, int startY);

    static void drawFrame(M5Canvas& canvas, const char** frame, uint8_t lines, bool blink = false, bool faceRight = true, bool sniff = false);
    static void drawGrass(M5Canvas& canvas);
    static void updateGrass();
};
