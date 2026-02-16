// Piglet ASCII avatar implementation

#include "avatar.h"
#include "weather.h"
#include "../ui/display.h"
#include <time.h>

// Static members
AvatarState Avatar::currentState = AvatarState::NEUTRAL;
bool Avatar::isBlinking = false;
bool Avatar::earsUp = true;
uint32_t Avatar::lastBlinkTime = 0;
uint32_t Avatar::blinkInterval = 3000;
int Avatar::moodIntensity = 0;  // Phase 8: -100 to 100

// Cute jump state
bool Avatar::jumpActive = false;
uint32_t Avatar::jumpStartTime = 0;

// Attack hop state (multi-hop pounce for captures)
bool Avatar::attackHopActive = false;
uint32_t Avatar::attackHopStartTime = 0;
uint8_t Avatar::attackHopIndex = 0;
uint8_t Avatar::attackHopTotal = 0;
int16_t Avatar::attackHopOriginX = 0;
int16_t Avatar::attackHopTargets[5] = {0};

// Walk transition state
bool Avatar::transitioning = false;
uint32_t Avatar::transitionStartTime = 0;
int Avatar::transitionFromX = 2;
int Avatar::transitionToX = 2;
bool Avatar::transitionToFacingRight = true;
int Avatar::currentX = 2;

// Sniff animation state
bool Avatar::isSniffing = false;
static uint32_t sniffStartTime = 0;
static const uint32_t SNIFF_DURATION_MS = 600;  // 600ms for proper sniff cycle
static uint8_t sniffFrame = 0;  // Alternates between nose shapes (oo, oO, Oo)

// Walk transition timing
static const uint32_t TRANSITION_DURATION_MS = 1200;  // 1.2s slow relaxed walk (was 400ms - too hectic)

// Rest cooldown after grass stops - prevents immediate re-triggering
static uint32_t lastGrassStopTime = 0;
static const uint32_t GRASS_REST_COOLDOWN_MS = 3000;  // 3 second chill period after grass stops

// Grass wander state (random roaming toward center while treadmill runs)
static uint32_t grassWanderTimer = 0;
static uint32_t grassWanderInterval = 4000;

// Attack shake state (visual feedback for captures)
static bool attackShakeActive = false;
static bool attackShakeStrong = false;
static uint32_t attackShakeRefreshTime = 0;

// Thunder flash state (weather effect - invert colors)
static bool thunderFlashActive = false;

// Night sky star system state
Avatar::Star Avatar::stars[15] = {{0}};
uint8_t Avatar::starCount = 0;
uint32_t Avatar::lastStarSpawn = 0;
uint32_t Avatar::nextSpawnDelay = 2000;
bool Avatar::starsActive = false;
uint32_t Avatar::lastNightCheck = 0;
bool Avatar::cachedNightMode = false;

// Color helper for thunder flash inversion (matches Sirloin)
static uint16_t getDrawColor() {
    if (thunderFlashActive) {
        return getColorBG();  // Swap: draw with BG color during flash
    }
    return getColorFG();
}

static uint16_t getBGColor() {
    if (thunderFlashActive) {
        return getColorFG();  // Inverted while flashing
    }
    return getColorBG();
}

// Grass animation state
bool Avatar::grassMoving = false;
bool Avatar::grassDirection = true;  // true = grass scrolls right
bool Avatar::pendingGrassStart = false;  // Wait for transition before starting grass
uint32_t Avatar::lastGrassUpdate = 0;
uint16_t Avatar::grassSpeed = 80;  // Default fast for OINK
Avatar::GrassBlade Avatar::grassBlades[GRASS_BLADE_COUNT] = {{0}};
int16_t Avatar::grassOffset = 0;
// Internal state for looking direction
static bool facingRight = true;  // Default: pig looks right
static uint32_t lastFlipTime = 0;
static uint32_t flipInterval = 5000;

// Look behavior (stationary observation)
static uint32_t lastLookTime = 0;
static uint32_t lookInterval = 2000;  // Look around every 2-5s when stationary
bool Avatar::onRightSide = false;  // Track which side of screen pig is on (class static)

// --- DERPY STYLE with direction ---
// Right-looking frames (snout 00 on right side of face, pig looks RIGHT)
const char* AVATAR_NEUTRAL_R[] = {
    " ?  ? ",
    "(o 00)",
    "(    )"
};

const char* AVATAR_HAPPY_R[] = {
    " ^  ^ ",
    "(^ 00)",
    "(    )"
};

const char* AVATAR_EXCITED_R[] = {
    " !  ! ",
    "(@ 00)",
    "(    )"
};

const char* AVATAR_HUNTING_R[] = {
    " |  | ",
    "(= 00)",
    "(    )"
};

const char* AVATAR_SLEEPY_R[] = {
    " v  v ",
    "(- 00)",
    "(    )"
};

const char* AVATAR_SAD_R[] = {
    " .  . ",
    "(T 00)",
    "(    )"
};

const char* AVATAR_ANGRY_R[] = {
    " \\  / ",
    "(# 00)",
    "(    )"
};

// Left-looking frames (snout 00 on left side of face, pig looks LEFT, z pigtail)
const char* AVATAR_NEUTRAL_L[] = {
    " ?  ? ",
    "(00 o)",
    "(    )z"
};

const char* AVATAR_HAPPY_L[] = {
    " ^  ^ ",
    "(00 ^)",
    "(    )z"
};

const char* AVATAR_EXCITED_L[] = {
    " !  ! ",
    "(00 @)",
    "(    )z"
};

const char* AVATAR_HUNTING_L[] = {
    " |  | ",
    "(00 =)",
    "(    )z"
};

const char* AVATAR_SLEEPY_L[] = {
    " v  v ",
    "(00 -)",
    "(    )z"
};

const char* AVATAR_SAD_L[] = {
    " .  . ",
    "(00 T)",
    "(    )z"
};

const char* AVATAR_ANGRY_L[] = {
    " \\  / ",
    "(00 #)",
    "(    )z"
};

void Avatar::init() {
    currentState = AvatarState::NEUTRAL;
    isBlinking = false;
    isSniffing = false;
    earsUp = true;
    lastBlinkTime = millis();
    blinkInterval = random(4000, 8000);
    
    // Init direction - start at LEFT or RIGHT edge (not center)
    // This ensures bubble can float beside pig from the start
    bool startRight = random(0, 2) == 0;
    onRightSide = startRight;
    currentX = startRight ? 180 : 25;  // Start at proper edge position (scaled for 320px)
    facingRight = !startRight;  // Face toward center (more interesting)
    lastFlipTime = millis();
    flipInterval = random(25000, 50000);  // First walk: 25-50s
    lastLookTime = millis();
    lookInterval = random(3000, 8000);  // First look: 3-8s (let pig settle in)
    
    // Init grass blade system
    grassMoving = false;
    grassDirection = true;
    pendingGrassStart = false;
    grassSpeed = 80;
    lastGrassUpdate = millis();
    lastGrassStopTime = 0;  // No cooldown on fresh init
    grassOffset = 0;
    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        grassBlades[i].height = random(6, 15);
        grassBlades[i].lean = random(-3, 4);
        grassBlades[i].width = random(1, 4);
    }

    // Init star system, dormant until night
    starsActive = false;
    starCount = 0;
    lastStarSpawn = 0;
    nextSpawnDelay = 2000;
    lastNightCheck = 0;
    cachedNightMode = false;
    initStarPositions();
}

void Avatar::setState(AvatarState state) {
    currentState = state;
}

void Avatar::setMoodIntensity(int intensity) {
    moodIntensity = constrain(intensity, -100, 100);
}

bool Avatar::isFacingRight() {
    return facingRight;
}

bool Avatar::isOnRightSide() {
    return onRightSide;
}

bool Avatar::isTransitioning() {
    return transitioning || attackHopActive;
}

int Avatar::getCurrentX() {
    return currentX;
}

void Avatar::blink() {
    isBlinking = true;
}

void Avatar::wiggleEars() {
    earsUp = !earsUp;
}

void Avatar::sniff() {
    if (!isSniffing) {
        sniffFrame = 0;  // Reset frame on new sniff
    }
    isSniffing = true;
    sniffStartTime = millis();
}

void Avatar::cuteJump() {
    // Trigger a cute celebratory jump - higher and slower than walk bounce
    jumpActive = true;
    jumpStartTime = millis();
}

void Avatar::attackHop() {
    // Multi-hop pounce animation for capture events
    attackHopActive = true;
    attackHopStartTime = millis();
    attackHopIndex = 0;
    attackHopOriginX = currentX;
    attackHopTotal = random(3, 6);  // 3-5 hops

    int16_t prevX = currentX;
    for (uint8_t i = 0; i < attackHopTotal; i++) {
        if (i == attackHopTotal - 1) {
            // Last hop returns home
            attackHopTargets[i] = attackHopOriginX;
        } else {
            // Random ±25–55px from previous position, clamped to [10, 200]
            int16_t offset = random(25, 56);
            if (random(0, 2) == 0) offset = -offset;
            int16_t target = prevX + offset;
            if (target < 10) target = 10;
            if (target > 200) target = 200;
            attackHopTargets[i] = target;
        }
        prevX = attackHopTargets[i];
    }
}

bool Avatar::isAttackHopping() {
    return attackHopActive;
}

void Avatar::draw(M5Canvas& canvas) {
    uint32_t now = millis();
    
    // Sniff animation times out after SNIFF_DURATION_MS
    // Update sniff frame for animation (cycle every 100ms)
    if (isSniffing) {
        if (now - sniffStartTime > SNIFF_DURATION_MS) {
            isSniffing = false;
            sniffFrame = 0;
        } else {
            sniffFrame = ((now - sniffStartTime) / 100) % 3;  // 3 frames: oo, oO, Oo
        }
    }
    
    // Handle walk transition animation
    if (transitioning) {
        uint32_t elapsed = now - transitionStartTime;
        if (elapsed >= TRANSITION_DURATION_MS) {
            // Transition complete
            transitioning = false;
            currentX = transitionToX;
            facingRight = transitionToFacingRight;
            onRightSide = (currentX > 110);  // Track which side we're on (midpoint of 10-200 range)
            
            // Start grass now if it was pending
            if (pendingGrassStart) {
                grassMoving = true;
                pendingGrassStart = false;
                facingRight = !grassDirection;  // Face opposite to grass movement
            } else {
                // === POST-WALK RANDOM BEHAVIOR ===
                // After arriving somewhere, pig might do something interesting
                int arrivalRoll = random(0, 100);
                if (arrivalRoll < 20) {
                    // 20%: Look around after arriving (curious)
                    facingRight = !facingRight;
                } else if (arrivalRoll < 35) {
                    // 15%: Sniff the new area
                    sniff();
                } else if (arrivalRoll < 45) {
                    // 10%: Quick ear wiggle (settling in)
                    wiggleEars();
                } else if (arrivalRoll < 55) {
                    // 10%: Turn around completely (changed mind)
                    facingRight = !transitionToFacingRight;
                }
                // 45%: Just face the direction we were walking
            }
            
            // Reset look timer for new position with random delay
            lastLookTime = now;
            lookInterval = random(1500, 6000);  // More variable
        } else {
            // Animate X position (ease in-out)
            float t = (float)elapsed / TRANSITION_DURATION_MS;
            // Heavy quintic ease: 6t^5 - 15t^4 + 10t^3 (more inertia than smooth step)
            // Flatter acceleration/deceleration = heavier feel
            float smoothT = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            currentX = transitionFromX + (int)((transitionToX - transitionFromX) * smoothT);
        }
    }

    // Phase 8: Mood intensity affects animation timing
    // High positive = excited (faster blinks, more looking around)
    // High negative = lethargic (slower blinks, less movement)
    
    // Calculate intensity-adjusted blink interval
    // Base: 4000-8000ms, excited (-50%): 2000-4000ms, sad (+50%): 6000-12000ms
    float blinkMod = 1.0f - (moodIntensity / 200.0f);  // 0.5 to 1.5
    uint32_t minBlink = (uint32_t)(4000 * blinkMod);
    uint32_t maxBlink = (uint32_t)(8000 * blinkMod);
    
    // Check if we should blink (single frame blink)
    if (now - lastBlinkTime > blinkInterval) {
        isBlinking = true;
        lastBlinkTime = now;
        blinkInterval = random(minBlink, maxBlink);
    }

    // Calculate intensity-adjusted intervals
    // Excited pig looks around more, sad pig stares
    // NOTE: Reduced mood effect (was /150, now /300) to prevent frantic movement at high happiness
    float flipMod = 1.0f - (moodIntensity / 300.0f);  // ~0.66 to ~1.33
    uint32_t minWalk = (uint32_t)(30000 * flipMod);   // Walk timer: 30s base
    uint32_t maxWalk = (uint32_t)(75000 * flipMod);   // Walk timer: 75s base
    uint32_t minLook = (uint32_t)(4000 * flipMod);    // Look timer: 4s base (more frequent looks)
    uint32_t maxLook = (uint32_t)(15000 * flipMod);   // Look timer: 15s base
    
    // === ORGANIC RANDOM BEHAVIORS ===
    // Disable all movement while grass is moving (treadmill mode) or attack hopping
    if (!transitioning && !grassMoving && !pendingGrassStart && !attackHopActive) {
        
        // --- LOOK BEHAVIOR: Random glances with personality ---
        if (now - lastLookTime > lookInterval) {
            int lookRoll = random(0, 100);
            
            if (lookRoll < 35) {
                // 35%: Simple head turn
                facingRight = !facingRight;
            } else if (lookRoll < 55) {
                // 20%: Look one way, then back (curious double-take)
                facingRight = !facingRight;
                // Schedule a quick look-back by shortening next interval
                lookInterval = random(800, 1500);  // Quick follow-up
                lastLookTime = now;
                goto skip_look_reset;  // Don't reset with normal interval
            } else if (lookRoll < 70) {
                // 15%: Sniff while looking (something caught attention)
                facingRight = random(0, 2) == 0;  // Random direction
                sniff();
            } else if (lookRoll < 82) {
                // 12%: Ear wiggle (alert/listening)
                wiggleEars();
            } else if (lookRoll < 90) {
                // 8%: Blink slowly (relaxed)
                blink();
            }
            // 10%: Do nothing (just chill)
            
            lastLookTime = now;
            // Vary the interval more - sometimes rapid, sometimes long pauses
            if (random(0, 5) == 0) {
                lookInterval = random(1500, 4000);  // 20% chance: quick succession
            } else {
                lookInterval = random(minLook, maxLook);
            }
        }
        skip_look_reset:
        
        // --- WALK BEHAVIOR: Always stay at LEFT/RIGHT edges for bubble positioning ---
        // Pig should ALWAYS be at edges where bubble can float beside, never in center
        if (now - lastFlipTime > flipInterval) {
            int walkRoll = random(0, 100);
            int targetX;
            
            // Define edge zones (bubble floats beside pig, not above)
            const int LEFT_EDGE = 10;   // Left rest position (320px screen)
            const int RIGHT_EDGE = 200; // Right rest position (pig body ~126px at size 3)

            if (walkRoll < 50) {
                // 50%: Walk to opposite edge (primary behavior)
                targetX = onRightSide ? LEFT_EDGE : RIGHT_EDGE;
            } else if (walkRoll < 85) {
                // 35%: Walk to random edge (left or right)
                targetX = random(0, 2) == 0 ? LEFT_EDGE : RIGHT_EDGE;
            } else if (walkRoll < 95) {
                // 10%: Short shuffle within current edge zone
                if (onRightSide) {
                    targetX = random(170, 200);  // Stay in right zone (320px)
                } else {
                    targetX = random(10, 45);    // Stay in left zone (320px)
                }
            } else {
                // 5%: Stay put, just turn around (fake walk)
                facingRight = !facingRight;
                lastFlipTime = now;
                flipInterval = random(minWalk / 2, maxWalk / 2);
                goto skip_walk;
            }
            
            // Only walk if destination is different enough (avoid micro-movements)
            if (abs(targetX - currentX) > 15) {
                bool goingRight = targetX > currentX;
                
                transitioning = true;
                transitionStartTime = now;
                transitionFromX = currentX;
                transitionToX = targetX;
                transitionToFacingRight = goingRight;  // Face direction of travel
                
                lastFlipTime = now;
                // Vary wait time more randomly
                if (random(0, 4) == 0) {
                    flipInterval = random(15000, 30000);  // 25% chance: shorter wait
                } else {
                    flipInterval = random(minWalk, maxWalk);
                }
            } else {
                // Destination too close, just look that way instead
                facingRight = targetX > currentX;
                lastFlipTime = now;
                flipInterval = random(minWalk / 3, minWalk);
            }
        }
        skip_walk:;
    }

    // === GRASS WANDER: Random roaming toward center while treadmill runs ===
    if (!transitioning && grassMoving && !attackHopActive) {
        if (now - grassWanderTimer > grassWanderInterval) {
            int homeX = grassDirection ? 180 : 25;
            int centerX = DISPLAY_W / 2;  // 160 = screen center limit
            int distFromHome = abs(currentX - homeX);

            if (distFromHome < 20) {
                // Near home - chance to wander toward center
                if (random(0, 100) < 35) {
                    int lo = (homeX < centerX) ? homeX + 10 : centerX;
                    int hi = (homeX < centerX) ? centerX : homeX - 10;
                    int target = random(lo, hi + 1);
                    if (abs(target - currentX) > 10) {
                        transitioning = true;
                        transitionStartTime = now;
                        transitionFromX = currentX;
                        transitionToX = target;
                        transitionToFacingRight = !grassDirection;  // Keep treadmill facing
                    }
                }
            } else {
                // Away from home - maybe return (or stay and chill)
                if (random(0, 100) < 45) {
                    transitioning = true;
                    transitionStartTime = now;
                    transitionFromX = currentX;
                    transitionToX = homeX;
                    transitionToFacingRight = !grassDirection;  // Restore treadmill facing
                }
            }

            grassWanderTimer = now;
            grassWanderInterval = random(3000, 8000);
        }
    }

    // Select frame based on state and direction (blink modifies eye only, not ears)
    const char** frame;
    bool shouldBlink = isBlinking && currentState != AvatarState::SLEEPY;
    
    // Clear blink flag after reading (single frame blink)
    if (isBlinking) {
        isBlinking = false;
    }
    
    switch (currentState) {
        case AvatarState::HAPPY:    
            frame = facingRight ? AVATAR_HAPPY_R : AVATAR_HAPPY_L; break;
        case AvatarState::EXCITED:  
            frame = facingRight ? AVATAR_EXCITED_R : AVATAR_EXCITED_L; break;
        case AvatarState::HUNTING:  
            frame = facingRight ? AVATAR_HUNTING_R : AVATAR_HUNTING_L; break;
        case AvatarState::SLEEPY:   
            frame = facingRight ? AVATAR_SLEEPY_R : AVATAR_SLEEPY_L; break;
        case AvatarState::SAD:      
            frame = facingRight ? AVATAR_SAD_R : AVATAR_SAD_L; break;
        case AvatarState::ANGRY:    
            frame = facingRight ? AVATAR_ANGRY_R : AVATAR_ANGRY_L; break;
        default:                    
            frame = facingRight ? AVATAR_NEUTRAL_R : AVATAR_NEUTRAL_L; break;
    }
    
    drawFrame(canvas, frame, 3, shouldBlink, facingRight, isSniffing);
}

void Avatar::drawFrame(M5Canvas& canvas, const char** frame, uint8_t lines, bool blink, bool faceRight, bool sniff) {
    // Star system background layer (behind pig)
    updateStars();
    drawStars(canvas);
    fillPigBoundingBox(canvas);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(3);
    canvas.setTextColor(getDrawColor());  // Thunder-aware color
    
    uint32_t now = millis();
    
    // Watchdog: if caller stops refreshing attack shake, auto-disable after 250ms
    if (attackShakeRefreshTime == 0 || (now - attackShakeRefreshTime) > 250) {
        attackShakeActive = false;
        attackShakeStrong = false;
    }
    
    // Handle cute jump timeout
    if (jumpActive && (now - jumpStartTime > JUMP_DURATION_MS)) {
        jumpActive = false;
    }
    
    // === Attack hop animation update ===
    if (attackHopActive) {
        uint32_t hopElapsed = now - attackHopStartTime;
        uint32_t totalHopTime = (uint32_t)attackHopTotal * ATTACK_HOP_MS;

        if (hopElapsed >= totalHopTime) {
            // Animation complete - return to origin
            attackHopActive = false;
            currentX = attackHopOriginX;
        } else {
            // Determine current hop index
            uint8_t hopIdx = hopElapsed / ATTACK_HOP_MS;
            if (hopIdx >= attackHopTotal) hopIdx = attackHopTotal - 1;
            attackHopIndex = hopIdx;

            // Interpolate X: smooth-step from previous position to target
            float hopT = (float)(hopElapsed - hopIdx * ATTACK_HOP_MS) / (float)ATTACK_HOP_MS;
            // Smooth step: 3t^2 - 2t^3
            float smoothT = hopT * hopT * (3.0f - 2.0f * hopT);
            int16_t fromX = (hopIdx == 0) ? attackHopOriginX : attackHopTargets[hopIdx - 1];
            int16_t toX = attackHopTargets[hopIdx];
            currentX = fromX + (int)((toX - fromX) * smoothT);

            // Face hop direction
            facingRight = (toX > fromX);
        }
    }

    // Calculate vertical shake/jump offset
    int shakeY = 0;
    if (attackHopActive) {
        // Attack hop: parabolic arc per hop
        uint32_t hopElapsed = now - attackHopStartTime;
        uint32_t hopLocal = hopElapsed - (uint32_t)attackHopIndex * ATTACK_HOP_MS;
        float t = (float)hopLocal / (float)ATTACK_HOP_MS;
        float arc = 4.0f * t * (1.0f - t);  // 0 → 1 → 0
        shakeY = -(int)(arc * ATTACK_HOP_HEIGHT);
    } else if (jumpActive) {
        // Cute jump: smooth arc up and down (sine-like)
        // First half: go up, second half: come down
        uint32_t elapsed = now - jumpStartTime;
        float t = (float)elapsed / (float)JUMP_DURATION_MS;  // 0.0 to 1.0
        // Parabolic arc: peaks at t=0.5
        float arc = 4.0f * t * (1.0f - t);  // 0 → 1 → 0
        shakeY = -(int)(arc * JUMP_HEIGHT);  // Negative = up
    } else if (attackShakeActive) {
        // Combat shake: random ±4px (normal) / ±6px (strong)
        const int amp = attackShakeStrong ? 6 : 4;
        shakeY = (esp_random() % 2 == 0) ? amp : -amp;
    } else if (transitioning || grassMoving) {
        // Heavy walk bounce: 4-phase weighted pattern (heavier landing feel)
        // Phase: down(0) → up-overshoot(-3) → settle-low(-1) → settle-mid(-2)
        // 80ms per phase = 320ms full cycle, slower than Sirloin's snappy bounce
        static const int bouncePattern[4] = {0, -3, -1, -2};
        int phase = (now / 80) % 4;
        shakeY = bouncePattern[phase];
    }

    // Use animated currentX position (set during transition or at rest)
    int startX = currentX;
    int startY = 40 + shakeY;  // Pig sits in grass, 1px above info panel (Y=108)
    int lineHeight = 22;
    
    for (uint8_t i = 0; i < lines; i++) {
        // Handle body line (i=2) for dynamic tail
        if (i == 2) {
            char bodyLine[16];
            bool tailOnLeft = false;  // Track if tail prefix added (needs X offset)
            if (grassMoving || pendingGrassStart) {
                // Treadmill mode: always show tail
                if (faceRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Tail on left when facing right
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Tail on right when facing left
                }
            } else if (transitioning) {
                // During transition: show tail on trailing side
                bool movingRight = (transitionToX > transitionFromX);
                if (movingRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Tail trails on left
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Tail trails on right
                }
            } else {
                // Stationary: always show tail based on facing direction
                if (faceRight) {
                    strncpy(bodyLine, "z(    )", sizeof(bodyLine));  // Facing right, tail on left
                    tailOnLeft = true;
                } else {
                    strncpy(bodyLine, "(    )z", sizeof(bodyLine));  // Facing left, tail on right
                }
            }
            bodyLine[sizeof(bodyLine) - 1] = '\0';
            // When tail is on left (z prefix), offset X back by 1 char width (18px at size 3)
            // to keep body aligned with head
            int bodyX = tailOnLeft ? (startX - 18) : startX;
            canvas.drawString(bodyLine, bodyX, startY + i * lineHeight);
        } else if (i == 1 && (blink || sniff)) {
            // Face line - modify eye and/or nose
            // Face format: "(X 00)" for right-facing, "(00 X)" for left-facing
            char modifiedLine[16];
            strncpy(modifiedLine, frame[i], sizeof(modifiedLine) - 1);
            modifiedLine[sizeof(modifiedLine) - 1] = '\0';
            
            if (blink) {
                // Replace eye character with '-' for blink
                if (faceRight) {
                    modifiedLine[1] = '-';  // Eye position in "(X 00)"
                } else {
                    modifiedLine[4] = '-';  // Eye position in "(00 X)"
                }
            }
            
            if (sniff) {
                // Animated sniff - cycle through nose shapes
                char n1, n2;
                switch (sniffFrame) {
                    case 0: n1 = 'o'; n2 = 'o'; break;  // oo
                    case 1: n1 = 'o'; n2 = 'O'; break;  // oO
                    case 2: n1 = 'O'; n2 = 'o'; break;  // Oo
                    default: n1 = 'o'; n2 = 'o'; break;
                }
                // Nose is at positions 3-4 for right-facing "(X 00)"
                // Nose is at positions 1-2 for left-facing "(00 X)"
                if (faceRight) {
                    modifiedLine[3] = n1;
                    modifiedLine[4] = n2;
                } else {
                    modifiedLine[1] = n1;
                    modifiedLine[2] = n2;
                }
            }
            
            canvas.drawString(modifiedLine, startX, startY + i * lineHeight);
        } else {
            canvas.drawString(frame[i], startX, startY + i * lineHeight);
        }
    }
    
    // Draw grass below piglet
    drawGrass(canvas);
}

void Avatar::setGrassMoving(bool moving, bool directionRight) {
    // Early exit if already in requested state (prevents per-frame overhead)
    if (moving && (grassMoving || pendingGrassStart)) {
        return;  // Already moving or pending - don't interrupt
    }
    if (!moving && !grassMoving && !pendingGrassStart) {
        return;  // Already stopped
    }
    
    if (moving) {
        // COOLDOWN CHECK: Don't start grass if we just stopped
        // This prevents rapid on/off/on/off state changes from causing macarena
        uint32_t now = millis();
        if (lastGrassStopTime > 0 && (now - lastGrassStopTime) < GRASS_REST_COOLDOWN_MS) {
            return;  // Still in cooldown period - pig needs rest
        }
        
        grassDirection = directionRight;
        
        // Calculate correct treadmill position based on direction
        // Grass RIGHT: pig at X=180 (tail margin on right, 320px screen)
        // Grass LEFT: pig at X=25 (tail margin on left: 25-18=7)
        int targetX = directionRight ? 180 : 25;
        
        if (transitioning) {
            // Check if this is a coast-back transition (pig returning to rest at X=20)
            // Don't interrupt coast-back with grass start - let the pig chill first
            // This prevents the "macarena" bug where rapid state changes cause endless back-and-forth
            if (transitionToX == 20) {
                return;  // Coast-back in progress - pig needs a break
            }
            // Already sliding to grass position - queue grass
            pendingGrassStart = true;
            grassMoving = false;
        } else if (currentX != targetX) {
            // Not at correct treadmill position - slide there first
            startWindupSlide(targetX, directionRight);  // face direction of travel
            pendingGrassStart = true;
            grassMoving = false;
        } else {
            // Already at correct position - start grass immediately
            facingRight = !directionRight;
            grassMoving = true;
            pendingGrassStart = false;
        }
        
        // Clear cooldown since we successfully started
        lastGrassStopTime = 0;
        grassWanderTimer = millis();
        grassWanderInterval = random(3000, 8000);  // Initial delay before first wander
    } else {
        // Stop grass and coast back to resting position
        grassMoving = false;
        pendingGrassStart = false;
        
        // Start cooldown timer - pig needs rest before grass can start again
        lastGrassStopTime = millis();
        
        // Reset walk timer to prevent immediate post-coast walk trigger
        lastFlipTime = millis();
        
        // Coast back to left resting position (X=25 for tail margin)
        startWindupSlide(25, false);  // X=25, face left when done
    }
}

void Avatar::setGrassSpeed(uint16_t ms) {
    grassSpeed = ms;
}

void Avatar::resetGrass() {
    grassOffset = 0;
    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        grassBlades[i].height = random(6, 15);
        grassBlades[i].lean = random(-3, 4);
        grassBlades[i].width = random(1, 4);
    }
}

void Avatar::updateGrass() {
    if (!grassMoving) return;

    uint32_t now = millis();
    // Pixel-level scroll: shift 1px every grassSpeed/GRASS_STRIDE ms
    uint32_t pixelInterval = grassSpeed / GRASS_STRIDE;
    if (pixelInterval < 1) pixelInterval = 1;
    if (now - lastGrassUpdate < pixelInterval) return;
    lastGrassUpdate = now;

    // Smooth pixel scroll
    if (grassDirection) {
        grassOffset++;
        if (grassOffset >= GRASS_STRIDE) {
            grassOffset = 0;
            // Rotate blade array right by 1
            GrassBlade last = grassBlades[GRASS_BLADE_COUNT - 1];
            for (int i = GRASS_BLADE_COUNT - 1; i > 0; i--) {
                grassBlades[i] = grassBlades[i - 1];
            }
            grassBlades[0] = last;
        }
    } else {
        grassOffset--;
        if (grassOffset < 0) {
            grassOffset = GRASS_STRIDE - 1;
            // Rotate blade array left by 1
            GrassBlade first = grassBlades[0];
            for (int i = 0; i < GRASS_BLADE_COUNT - 1; i++) {
                grassBlades[i] = grassBlades[i + 1];
            }
            grassBlades[GRASS_BLADE_COUNT - 1] = first;
        }
    }

    // ~3% mutation chance — randomize one blade for organic variety
    if (random(0, 30) == 0) {
        int idx = random(0, GRASS_BLADE_COUNT);
        grassBlades[idx].height = random(6, 15);
        grassBlades[idx].lean = random(-3, 4);
        grassBlades[idx].width = random(1, 4);
    }
}

void Avatar::drawGrass(M5Canvas& canvas) {
    updateGrass();

    uint16_t color = getDrawColor();  // Thunder-aware color
    const int16_t baseY = 107;  // Ground line (info panel starts at 108)

    // Solid ground line
    canvas.fillRect(0, 106, 320, 2, color);

    // Draw triangle blades
    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        int16_t cx = i * GRASS_STRIDE + grassOffset;
        // Wrap for seamless scrolling
        if (cx < -GRASS_STRIDE) cx += 320 + GRASS_STRIDE;
        if (cx > 320) continue;

        const GrassBlade& b = grassBlades[i];
        int16_t tipX = cx + b.lean;
        int16_t tipY = baseY - b.height;
        int16_t leftX = cx - b.width;
        int16_t rightX = cx + b.width;
        canvas.fillTriangle(tipX, tipY, leftX, baseY, rightX, baseY, color);
    }
}

// --- Night sky star system ---
bool Avatar::isNightTime() {
    uint32_t now = millis();

    // Cache rtc reads, check every 60 seconds
    if (now - lastNightCheck < 60000 && lastNightCheck != 0) {
        return cachedNightMode;
    }
    lastNightCheck = now;

    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year >= 2024) {
        uint8_t hour = dt.time.hours;
        cachedNightMode = (hour >= 20 || hour < 6);
        return cachedNightMode;
    }

    time_t unixNow = time(nullptr);
    if (unixNow >= 1700000000) {
        struct tm timeinfo;
        localtime_r(&unixNow, &timeinfo);
        uint8_t hour = (uint8_t)timeinfo.tm_hour;
        cachedNightMode = (hour >= 20 || hour < 6);
        return cachedNightMode;
    }

    cachedNightMode = false;
    return false;
}

bool Avatar::areStarsActive() {
    return starsActive;
}

void Avatar::initStarPositions() {
    // Pre-gen star positions, hide until spawn
    for (uint8_t i = 0; i < MAX_STARS; i++) {
        // y 20-100 sky/backdrop, bubble still wins
        // x 5-315 near full width (320px screen)
        stars[i].x = random(5, 315);
        // Match rain clip: keep stars above grass (rain clips at y < 74)
        stars[i].y = random(6, 74);
        stars[i].size = 1;
        stars[i].brightness = 0;
        stars[i].fadeInStart = 0;
        // About 20 percent twinkle
        stars[i].isBlinking = (random(0, 100) < 20);
    }
}

void Avatar::updateStars() {
    uint32_t now = millis();

    // Never show stars while raining
    if (Weather::isRaining()) {
        if (starsActive) {
            starsActive = false;
            starCount = 0;
        }
        return;
    }

    // Night mode transition
    bool nightNow = isNightTime();

    if (nightNow && !starsActive) {
        // Night starting, spawn sequence online
        starsActive = true;
        starCount = 0;
        lastStarSpawn = now;
        nextSpawnDelay = random(800, 4001);  // 800ms to 4s
        initStarPositions();
    } else if (!nightNow && starsActive) {
        // Day starting, kill stars
        starsActive = false;
        starCount = 0;
    }

    if (!starsActive) return;

    // Spawn new star when timer expires
    if (starCount < MAX_STARS && (now - lastStarSpawn >= nextSpawnDelay)) {
        stars[starCount].fadeInStart = now;
        stars[starCount].brightness = 0;
        starCount++;
        lastStarSpawn = now;
        nextSpawnDelay = random(800, 4001);
    }

    // Update visible stars, fade-in, twinkle handled in draw
    for (uint8_t i = 0; i < starCount; i++) {
        uint32_t age = now - stars[i].fadeInStart;
        if (age < 500) {
            stars[i].brightness = (age * 255) / 500;
        } else {
            stars[i].brightness = 255;
        }
    }
}

void Avatar::fillPigBoundingBox(M5Canvas& canvas) {
    if (!starsActive || starCount == 0) return;

    int boxX = currentX - 25;
    int boxW = 155;  // covers tail + 7 chars + margin
    int boxY = 28;   // base y (40) minus jump headroom (12)
    int boxH = 79;   // covers Y=28–107, stops at ground line

    // Clamp to screen
    if (boxX < 0) { boxW += boxX; boxX = 0; }
    if (boxX + boxW > 320) boxW = 320 - boxX;

    canvas.fillRect(boxX, boxY, boxW, boxH, getBGColor());
}

void Avatar::drawStars(M5Canvas& canvas) {
    if (!starsActive || starCount == 0) return;

    uint32_t now = millis();
    uint16_t fg = getDrawColor();
    canvas.setTextSize(1);
    canvas.setTextColor(fg);
    canvas.setTextDatum(top_left);

    for (uint8_t i = 0; i < starCount; i++) {
        if (stars[i].brightness < 128) continue;
        if (stars[i].y >= 74) continue;  // Match rain clip above grass

        char starChar = '.';
        if (stars[i].isBlinking) {
            uint32_t phase = (now + i * 700) % 4000;
            if (phase >= 1700 && phase < 2300) {
                starChar = '*';
            }
        }
        canvas.drawChar(starChar, stars[i].x, stars[i].y);
    }
}

// --- Phase 8: Direction control helpers ---
void Avatar::setFacingLeft() {
    facingRight = false;
}

void Avatar::setFacingRight() {
    facingRight = true;
}

// --- Phase 2: Attack shake control ---
void Avatar::setAttackShake(bool active, bool strong) {
    attackShakeActive = active;
    attackShakeStrong = strong;
    attackShakeRefreshTime = active ? millis() : 0;
}

// --- Thunder flash control (weather effect) ---
void Avatar::setThunderFlash(bool active) {
    thunderFlashActive = active;
}

bool Avatar::isThunderFlashing() {
    return thunderFlashActive;
}

// --- Phase 6: Windup slide for coast-back ---
void Avatar::startWindupSlide(int targetX, bool faceRight) {
    // Start a smooth transition to target position
    // Uses standard TRANSITION_DURATION_MS (300ms) from draw() logic
    if (currentX != targetX) {
        transitioning = true;
        transitionFromX = currentX;
        transitionToX = targetX;
        transitionStartTime = millis();
        transitionToFacingRight = faceRight;
    }
    // Set facing direction for when transition completes
    facingRight = faceRight;
}
