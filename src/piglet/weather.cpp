// Weather effects module - clouds, rain, thunder, wind
// Mood-tied weather system ported from Sirloin

#include "weather.h"
#include "avatar.h"
#include "../ui/display.h"
#include <esp_random.h>

namespace Weather {

// === CLOUD SHAPE SYSTEM ===
struct CloudPuff {
    int8_t dx, dy;      // offset from cloud center
    uint8_t radius;     // circle radius (2-6px)
};

struct CloudShape {
    float x;            // current X position (float for smooth drift)
    int8_t y;           // Y center (5-11, keeps circles in Y 0-16 zone)
    uint8_t puffCount;  // 3-5 overlapping circles per cloud
    CloudPuff puffs[5];
    uint8_t scale;      // 0-255 growth animation (controls drawn radius)
    bool active;
    bool growing;       // scaling up toward 255
    bool shrinking;     // scaling down toward 0, deactivates at 0
};

static const uint8_t MAX_CLOUDS = 8;
static CloudShape clouds[MAX_CLOUDS];
static uint32_t lastCloudUpdate = 0;
static const uint16_t cloudSpeed = 14400;  // Ultra slow atmospheric drift (matches sirloin)
static uint32_t lastCloudParallax = 0;
static const uint8_t CLOUD_PARALLAX_GRASS_SHIFTS = 6;  // Shift clouds every N grass shifts
static uint32_t lastDensityCheck = 0;
static uint32_t lastScaleUpdate = 0;

// === RAIN STATE ===
struct RainDrop {
    float x;
    float y;
    uint8_t speed; // pixels per update for visible rain
};
static const int RAIN_DROP_COUNT = 25;  // Increased for denser rain
static RainDrop rainDrops[RAIN_DROP_COUNT] = {{0}};
static bool rainActive = false;
static bool rainDecided = false;  // Prevents per-frame re-randomization
static int lastMoodTier = -1;     // Track tier (not raw mood) with hysteresis
static uint32_t lastRainUpdate = 0;
static const uint16_t RAIN_SPEED_MS = 30;  // Fast updates for snappy rain

// === THUNDER STATE ===
static bool thunderFlashing = false;
static uint32_t lastThunderStorm = 0;
static uint32_t thunderFlashStart = 0;
static uint8_t thunderFlashesRemaining = 0;
static uint8_t thunderFlashState = 0;  // 0=off, 1=on
static uint32_t thunderMinInterval = 50000;  // 50-90s between storms (adjusts with mood)
static uint32_t thunderMaxInterval = 90000;

// === WIND STATE ===
struct WindParticle {
    float x;
    float y;
    float speed;        // px per update tick
    float spawnX;       // X at birth (for distance calc)
    float maxTravel;    // distance before vanishing (180-280px, randomized)
    uint8_t baseSize;   // initial pixel radius 1-3
    bool active;
    bool dirRight;      // true = moves right, false = moves left
};
static WindParticle windParticles[6] = {{0}};
static bool windActive = false;
static uint32_t lastWindGust = 0;
static uint32_t windGustDuration = 0;
static uint32_t windGustInterval = 15000;  // 15-30s between gusts
static uint32_t lastWindUpdate = 0;

// === MOOD-BASED WEATHER CONTROL ===
static int currentMood = 50;  // Cached mood level

// Forward declarations
static void generateCloudPuffs(CloudShape& cloud);
static void activateCloud();
static void deactivateCloud();

// === INITIALIZATION ===
void init() {
    // All clouds start inactive (zero-initialized static)
    for (int i = 0; i < MAX_CLOUDS; i++) {
        clouds[i].active = false;
    }

    // Init wind particles (inactive)
    for (int i = 0; i < 6; i++) {
        windParticles[i].active = false;
    }

    lastCloudUpdate = millis();
    lastCloudParallax = lastCloudUpdate;
    lastDensityCheck = lastCloudUpdate;
    lastScaleUpdate = lastCloudUpdate;
    lastWindGust = millis();
    lastThunderStorm = millis();
}

static void generateCloudPuffs(CloudShape& cloud) {
    // Center puff: large, anchors the cloud
    cloud.puffs[0] = {0, 0, (uint8_t)random(4, 6)};
    // Left flanking puff: slightly lower
    cloud.puffs[1] = {(int8_t)random(-10, -5), (int8_t)random(1, 3), (uint8_t)random(3, 5)};
    // Right flanking puff: slightly lower
    cloud.puffs[2] = {(int8_t)random(6, 11), (int8_t)random(1, 3), (uint8_t)random(3, 5)};
    cloud.puffCount = 3;

    // Optional top puff (70% chance)
    if (random(0, 100) < 70) {
        cloud.puffs[cloud.puffCount] = {(int8_t)random(-2, 3), (int8_t)random(-3, -1), (uint8_t)random(2, 4)};
        cloud.puffCount++;
    }
    // Optional extra side puff (50% chance)
    if (cloud.puffCount < 5 && random(0, 100) < 50) {
        int8_t side = random(0, 2) ? (int8_t)12 : (int8_t)-12;
        cloud.puffs[cloud.puffCount] = {(int8_t)(side + (int8_t)random(-2, 3)), (int8_t)random(0, 3), (uint8_t)random(2, 4)};
        cloud.puffCount++;
    }
}

static int getActiveCloudCount() {
    int count = 0;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (clouds[i].active) count++;
    }
    return count;
}

static int getTargetCloudCount() {
    // mood >= 20 -> 0, mood <= -80 -> 8, linear between
    int target = (20 - currentMood) * 8 / 100;
    if (target < 0) target = 0;
    if (target > 8) target = 8;
    return target;
}

static void activateCloud() {
    // Find an inactive slot
    int slot = -1;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    // Try to find a well-spaced X position (5 attempts, keep best)
    float bestX = (float)random(-20, 261);
    float bestDist = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        float tryX = (float)random(-20, 261);
        float minDist = 300.0f;
        for (int i = 0; i < MAX_CLOUDS; i++) {
            if (i == slot || !clouds[i].active) continue;
            float d = tryX - clouds[i].x;
            float dist = d < 0 ? -d : d;
            if (dist > 140.0f) dist = 280.0f - dist;  // wrap distance
            if (dist < minDist) minDist = dist;
        }
        if (minDist > bestDist) {
            bestDist = minDist;
            bestX = tryX;
        }
    }

    CloudShape& c = clouds[slot];
    c.x = bestX;
    c.y = (int8_t)random(5, 12);
    c.scale = 0;
    c.active = true;
    c.growing = true;
    c.shrinking = false;
    generateCloudPuffs(c);
}

static void deactivateCloud() {
    // Pick the active cloud furthest from screen center to shrink away
    float maxDist = -1;
    int pick = -1;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active || clouds[i].shrinking) continue;
        float d = clouds[i].x - 120.0f;  // 240px center
        float dist = d < 0 ? -d : d;
        if (dist > maxDist) {
            maxDist = dist;
            pick = i;
        }
    }
    if (pick >= 0) {
        clouds[pick].shrinking = true;
        clouds[pick].growing = false;
    }
}

// === WEATHER STATE CONTROL ===
// Determine which mood tier we're in (with hysteresis to prevent oscillation)
// Hysteresis: need to cross threshold by 5 points to change tier
static int getMoodTier(int mood, int currentTier) {
    // If no current tier, use raw thresholds
    if (currentTier < 0) {
        if (mood <= -40) return 2;      // SAD: high rain chance
        if (mood <= -20) return 1;      // MEH: low rain chance
        return 0;                        // HAPPY/NEUTRAL: no rain
    }

    // Apply hysteresis based on direction of change
    switch (currentTier) {
        case 0:  // Currently HAPPY - need to drop below -25 to become MEH
            if (mood <= -25) return (mood <= -45) ? 2 : 1;
            return 0;
        case 1:  // Currently MEH - need -45 for SAD, -15 for HAPPY
            if (mood <= -45) return 2;
            if (mood > -15) return 0;
            return 1;
        case 2:  // Currently SAD - need to rise above -35 to become MEH
            if (mood > -35) return (mood > -15) ? 0 : 1;
            return 2;
    }
    return 0;
}

void setMoodLevel(int momentum) {
    currentMood = momentum;
    int newTier = getMoodTier(momentum, lastMoodTier);

    // Only re-roll rain when actually changing tiers (not every frame!)
    bool shouldReroll = !rainDecided || (newTier != lastMoodTier);

    if (shouldReroll) {
        lastMoodTier = newTier;
        rainDecided = true;

        bool shouldRain = false;

        if (newTier == 2) {
            // SAD/DEPRESSED: 70% rain chance (increased)
            shouldRain = (random(0, 100) < 70);
            // More frequent storms
            thunderMinInterval = 30000;  // 30-60s
            thunderMaxInterval = 60000;
        } else if (newTier == 1) {
            // MEH: 35% rain chance (increased from 20%)
            shouldRain = (random(0, 100) < 35);
            // Occasional storms
            thunderMinInterval = 60000;  // 60-120s
            thunderMaxInterval = 120000;
        } else {
            // Happy/neutral: no rain, clear the sky
            shouldRain = false;
            thunderMinInterval = 999999;  // Effectively disabled
            thunderMaxInterval = 999999;
        }

        setRaining(shouldRain);
    }
}

void setRaining(bool active) {
    if (active && !rainActive) {
        // Spawn raindrops staggered across entire screen height for immediate rain
        for (int i = 0; i < RAIN_DROP_COUNT; i++) {
            rainDrops[i].x = (float)random(0, DISPLAY_W);
            // Distribute drops across visible area (stop above grass at Y=88)
            rainDrops[i].y = (float)random(16, 100);
            // Fast rain (5-8 pixels per update)
            rainDrops[i].speed = random(5, 9);
        }
    } else if (!active && rainActive) {
        // Stop any in-flight thunder to avoid stuck flash on clear skies
        thunderFlashing = false;
        thunderFlashState = 0;
        thunderFlashesRemaining = 0;
        lastThunderStorm = millis();
    }
    rainActive = active;
}

void triggerThunderStorm() {
    thunderFlashesRemaining = 3;
    lastThunderStorm = millis();
}

// Forward declarations for static update functions
static void updateClouds(uint32_t now);
static void updateRain(uint32_t now);
static void updateThunder(uint32_t now);
static void updateWind(uint32_t now);

// === ANIMATION UPDATES ===
void update() {
    uint32_t now = millis();

    // Update clouds (always)
    updateClouds(now);

    // Update rain (if active)
    if (rainActive) {
        updateRain(now);
    }

    // Update thunder (if raining)
    if (rainActive) {
        updateThunder(now);
    }

    // Update wind gusts (periodic)
    updateWind(now);
}

static void updateClouds(uint32_t now) {
    // Self-drift: slow rightward movement
    if (now - lastCloudUpdate >= cloudSpeed) {
        lastCloudUpdate = now;
        for (int i = 0; i < MAX_CLOUDS; i++) {
            if (clouds[i].active) clouds[i].x += 0.5f;
        }
    }

    // Parallax: when grass is moving, nudge clouds in the same direction (slower)
    if (Avatar::isGrassMoving()) {
        uint32_t parallaxInterval = (uint32_t)Avatar::getGrassSpeed() * CLOUD_PARALLAX_GRASS_SHIFTS;
        if (parallaxInterval < 150) parallaxInterval = 150;

        if (now - lastCloudParallax >= parallaxInterval) {
            lastCloudParallax = now;
            float shift = Avatar::isGrassDirectionRight() ? 1.0f : -1.0f;
            for (int i = 0; i < MAX_CLOUDS; i++) {
                if (clouds[i].active) clouds[i].x += shift;
            }
        }
    } else {
        lastCloudParallax = now;
    }

    // Wrap cloud positions: virtual range -40 to 280
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active) continue;
        if (clouds[i].x > 280.0f) clouds[i].x -= 320.0f;
        if (clouds[i].x < -40.0f) clouds[i].x += 320.0f;
    }

    // Density check: every 2 seconds, add or remove clouds to match mood
    if (now - lastDensityCheck >= 2000) {
        lastDensityCheck = now;
        int target = getTargetCloudCount();
        int active = getActiveCloudCount();
        if (active < target) {
            activateCloud();
        } else if (active > target) {
            deactivateCloud();
        }
    }

    // Scale animation: step growth/shrink every 80ms
    if (now - lastScaleUpdate >= 80) {
        lastScaleUpdate = now;
        for (int i = 0; i < MAX_CLOUDS; i++) {
            if (!clouds[i].active) continue;
            if (clouds[i].growing) {
                if (clouds[i].scale <= 240) {
                    clouds[i].scale += 15;
                } else {
                    clouds[i].scale = 255;
                    clouds[i].growing = false;
                }
            } else if (clouds[i].shrinking) {
                if (clouds[i].scale >= 15) {
                    clouds[i].scale -= 15;
                } else {
                    clouds[i].scale = 0;
                    clouds[i].active = false;
                    clouds[i].shrinking = false;
                }
            }
        }
    }
}

static void updateRain(uint32_t now) {
    if (now - lastRainUpdate < RAIN_SPEED_MS) return;
    lastRainUpdate = now;

    // Calculate horizontal drift based on grass movement (parallax effect)
    float horizontalDrift = 0.0f;
    if (Avatar::isGrassMoving()) {
        uint16_t grassSpeedMs = Avatar::getGrassSpeed();
        if (grassSpeedMs == 0) grassSpeedMs = 1;
        const float grassShiftPixels = 8.0f;  // GRASS_STRIDE: pixels per scroll step
        float grassPixelsPerMs = grassShiftPixels / (float)grassSpeedMs;
        float grassPixelsPerUpdate = grassPixelsPerMs * (float)RAIN_SPEED_MS;
        horizontalDrift = grassPixelsPerUpdate * 0.4f;  // 40% of grass speed
        if (Avatar::isGrassDirectionRight()) {
            horizontalDrift *= -1.0f;
        }
    }

    for (int i = 0; i < RAIN_DROP_COUNT; i++) {
        rainDrops[i].y += (float)rainDrops[i].speed;
        rainDrops[i].x += horizontalDrift;

        // Wrap horizontally if drifted off screen
        if (rainDrops[i].x < 0.0f) rainDrops[i].x += (float)DISPLAY_W;
        if (rainDrops[i].x >= (float)DISPLAY_W) rainDrops[i].x -= (float)DISPLAY_W;

        // Respawn just below clouds when reaching bottom
        // Grass starts at Y=91, stop rain 3px above it
        if (rainDrops[i].y >= 103.0f) {
            rainDrops[i].y = (float)random(16, 23);  // Just below cloud layer
            rainDrops[i].x = (float)random(0, DISPLAY_W);
            rainDrops[i].speed = random(5, 9);  // Fast rain
        }
    }
}

static void updateThunder(uint32_t now) {
    // Check if time for new storm
    if (!thunderFlashing && thunderFlashesRemaining == 0) {
        if (now - lastThunderStorm > thunderMinInterval) {
            uint32_t interval = random(thunderMinInterval, thunderMaxInterval);
            if (now - lastThunderStorm >= interval) {
                // Start new storm
                thunderFlashesRemaining = random(2, 4);  // 2-3 flashes
                lastThunderStorm = now;
            }
        }
    }

    // Execute flash sequence
    if (thunderFlashesRemaining > 0 && !thunderFlashing) {
        thunderFlashing = true;
        thunderFlashStart = now;
        thunderFlashState = 1;  // Flash ON
        thunderFlashesRemaining--;
    }

    if (thunderFlashing) {
        uint32_t elapsed = now - thunderFlashStart;

        // Faster flicker: shorter ON/OFF windows
        if (thunderFlashState == 1 && elapsed > random(30, 60)) {
            // Turn flash OFF
            thunderFlashState = 0;
            thunderFlashStart = now;
        } else if (thunderFlashState == 0 && elapsed > random(20, 40)) {
            // Flash complete
            thunderFlashing = false;
            thunderFlashState = 0;
        }
    }
}

static void updateWind(uint32_t now) {
    // Suppress wind during rain
    if (rainActive) {
        if (windActive) {
            windActive = false;
            for (int i = 0; i < 6; i++) windParticles[i].active = false;
        }
        lastWindGust = now;  // Reset so gust doesn't fire immediately after rain stops
        return;
    }

    // Check for new wind gust
    if (!windActive && now - lastWindGust > windGustInterval) {
        bool grassOn = Avatar::isGrassMoving();
        int spawnChance = grassOn ? 70 : 20;

        if ((int)random(0, 100) < spawnChance) {
            windActive = true;
            windGustDuration = random(2000, 4000);
            lastWindGust = now;

            bool goRight = grassOn ? Avatar::isGrassDirectionRight() : (random(0, 2) == 0);

            for (int i = 0; i < 6; i++) {
                float spawnX = goRight
                    ? (-5.0f - random(0, 40))
                    : ((float)DISPLAY_W + 5.0f + random(0, 40));
                windParticles[i].x = spawnX;
                windParticles[i].spawnX = spawnX;
                windParticles[i].y = (float)random(20, 105);
                windParticles[i].speed = 2.0f + (float)random(0, 30) / 10.0f;  // 2.0-5.0
                windParticles[i].maxTravel = (float)random(180, 281);
                windParticles[i].baseSize = random(1, 4);  // 1-3 px radius
                windParticles[i].active = true;
                windParticles[i].dirRight = goRight;
            }
        } else {
            windGustInterval = grassOn ? random(3000, 8000) : random(15000, 30000);
            lastWindGust = now;
        }
    }

    // Update active wind particles
    if (windActive) {
        if (now - lastWindGust > windGustDuration) {
            // Gust finished
            windActive = false;
            windGustInterval = Avatar::isGrassMoving() ? random(3000, 8000) : random(15000, 30000);
            for (int i = 0; i < 6; i++) {
                windParticles[i].active = false;
            }
        } else {
            // Animate particles with directional movement
            if (now - lastWindUpdate > 50) {
                lastWindUpdate = now;
                for (int i = 0; i < 6; i++) {
                    if (!windParticles[i].active) continue;
                    float dir = windParticles[i].dirRight ? 1.0f : -1.0f;
                    windParticles[i].x += windParticles[i].speed * dir;
                    windParticles[i].y += (random(0, 3) - 1) * 0.5f;  // vertical wobble

                    float dist = windParticles[i].x - windParticles[i].spawnX;
                    if (dist < 0) dist = -dist;
                    if (dist >= windParticles[i].maxTravel) {
                        windParticles[i].active = false;
                    }
                }
            }
        }
    }
}

// === THUNDER FLASH QUERY ===
bool isThunderFlashing() {
    return thunderFlashing && thunderFlashState == 1;
}

bool isRaining() {
    return rainActive;
}

// === DRAWING ===
void drawClouds(M5Canvas& canvas, uint16_t colorFG) {
    // During thunder flash, use inverted color (matches sirloin's getDrawColor)
    uint16_t drawColor = isThunderFlashing() ? getColorBG() : colorFG;

    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active || clouds[i].scale == 0) continue;
        float scaleFactor = (float)clouds[i].scale / 255.0f;

        for (int p = 0; p < clouds[i].puffCount; p++) {
            int r = (int)((float)clouds[i].puffs[p].radius * scaleFactor + 0.5f);
            if (r < 1) continue;
            int cx = (int)(clouds[i].x + clouds[i].puffs[p].dx);
            int cy = clouds[i].y + clouds[i].puffs[p].dy;
            canvas.fillCircle(cx, cy, r, drawColor);

            // Draw wrap ghost if near screen edges for seamless scrolling
            if (cx - r < 20 && clouds[i].x < 0) {
                canvas.fillCircle(cx + 320, cy, r, drawColor);
            } else if (cx + r > 220 && clouds[i].x > 240) {
                canvas.fillCircle(cx - 320, cy, r, drawColor);
            }
        }
    }
}

void draw(M5Canvas& canvas, uint16_t colorFG, uint16_t colorBG) {
    // During thunder flash, invert colors for rain/wind (matches sirloin)
    uint16_t drawColor = isThunderFlashing() ? colorBG : colorFG;

    // Draw rain
    if (rainActive) {
        for (int i = 0; i < RAIN_DROP_COUNT; i++) {
            int x = (int)rainDrops[i].x;
            int y = (int)rainDrops[i].y;

            // Skip if above visible area (drops falling into view)
            if (y < 0) continue;

            // Draw 6-pixel tall x 2-pixel wide raindrop (slightly taller for visibility)
            for (int dy = 0; dy < 6; dy++) {
                if (y + dy < 103) {  // Clip 3px above grass (grass starts at Y=106)
                    canvas.drawPixel(x, y + dy, drawColor);
                    if (x + 1 < DISPLAY_W) canvas.drawPixel(x + 1, y + dy, drawColor);
                }
            }
        }
    }

    // Draw wind particles (shrinking filled circles)
    if (windActive) {
        for (int i = 0; i < 6; i++) {
            if (!windParticles[i].active) continue;
            int x = (int)windParticles[i].x;
            int y = (int)windParticles[i].y;
            if (x < -3 || x > DISPLAY_W + 3) continue;

            // Size shrinks to 0 over maxTravel distance
            float dist = windParticles[i].x - windParticles[i].spawnX;
            if (dist < 0) dist = -dist;
            float progress = dist / windParticles[i].maxTravel;
            if (progress > 1.0f) progress = 1.0f;
            int r = (int)((float)windParticles[i].baseSize * (1.0f - progress) + 0.5f);
            if (r < 1) continue;

            canvas.fillCircle(x, y, r, drawColor);
        }
    }
}

}  // namespace Weather
