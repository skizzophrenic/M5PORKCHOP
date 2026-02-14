// m5porkchop
// Main entry point
// by 0ct0

#include <M5Unified.h>
#include <SD.h>
#include <WiFi.h>              // <-- PATCH: init WiFi early (before heap fragmentation)
#include <esp_heap_caps.h>     // For heap conditioning
#include <string.h>            // For memset
#include "core/porkchop.h"
#include "core/config.h"
#include "core/xp.h"
#include "core/sdlog.h"
#include "core/wifi_utils.h"
#include "core/heap_policy.h"
#include "core/heap_health.h"
#include "core/network_recon.h"
#include "ui/display.h"
#include "ui/input.h"
#include "gps/gps.h"
#include "piglet/avatar.h"
#include "piglet/mood.h"
#include "modes/oink.h"
#include "modes/warhog.h"
#include "core/janus_hog.h"
#include "audio/sfx.h"

Porkchop porkchop;


void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== PORKCHOP STARTING ===");

    Serial.printf("[BOOT] PSRAM: size=%u free=%u\n",
                  (unsigned)ESP.getPsramSize(),
                  (unsigned)ESP.getFreePsram());

    // Init hardware
    auto cfg = M5.config();
    M5.begin(cfg);

    // Load configuration from SD
    if (!Config::init()) {
        Serial.println("[MAIN] Config init failed, using defaults");
    }

    // Init SD logging (will be enabled via settings if user wants)
    SDLog::init();

    // Load previous session watermarks before resetting peaks
    HeapHealth::loadPreviousSession();

    // TLS reserve disabled: browser handles TLS, keep heap for UI/file transfer.

    // Init display system
    Display::init();
    Input::init();

    // Init audio early so boot sound plays
    SFX::init();

    // Show boot splash (3 screens: OINK OINK, MY NAME IS, PORKCHOP)
    Display::showBootSplash();

    // Apply saved brightness
    M5.Display.setBrightness(Config::personality().brightness * 255 / 100);

    // Initialize piglet personality
    Avatar::init();
    Mood::init();

    // Initialize GPS (if enabled)
    if (Config::gps().enabled) {
        GPS::init(Config::gps().rxPin, Config::gps().txPin, Config::gps().baudRate);
    }

    // Initialize JanusHog coprocessor (JANUS HOG) — before modes, after GPS
    JanusHog::init();

    // Initialize modes
    OinkMode::init();
    WarhogMode::init();
    porkchop.init();

    Serial.println("=== PORKCHOP READY ===");
    Serial.printf("Piglet: %s\n", Config::personality().name);
    
    // #region agent log
    // [DEBUG] H1: Log heap after init to check static pool impact (~13KB expected reduction)
    Serial.printf("[DBG-HEAP] After init: free=%u largest=%u\n", 
                  (unsigned)ESP.getFreeHeap(), 
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    // #endregion
    
    // Start background network reconnaissance service
    // This stabilizes heap by running WiFi promiscuous mode early
    // and provides shared network data for OINK/DONOHAM/SPECTRUM modes
    NetworkRecon::start();

    // Reset heap health baseline to post-init state so the health bar
    // starts at the REAL value, not 100%. Without this, the EMA slowly
    // converges from 100% to reality, looking like a steady decline.
    HeapHealth::resetPeaks(true);
}

void loop() {
    M5.update();
    Input::update();
    
    // #region agent log
    // [DEBUG] H1/H3: Periodic heap monitoring (every 5 seconds)
    static uint32_t lastHeapLog = 0;
    if (millis() - lastHeapLog > 5000) {
        lastHeapLog = millis();
        Serial.printf("[DBG-HEAP-LOOP] free=%u largest=%u minFree=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                      (unsigned)ESP.getMinFreeHeap());
    }
    // #endregion

    // Persist session watermarks to SD (rate-limited to 60s internally)
    HeapHealth::persistWatermarks();

    {
        static bool bakedActive = false;
        static uint32_t bakedStartMs = 0;
        static uint32_t bakedDurationMs = 0;
        static bool bakedTriggered = false;
        static uint32_t lastBakedCheck = 0;

        if (bakedActive) {
            if (millis() - bakedStartMs >= bakedDurationMs) {
                bakedActive = false;
            } else {
                yield();
                return;
            }
        }

        if (!bakedTriggered && XP::hasUnlockable(3) && millis() - lastBakedCheck > 1000) {
            lastBakedCheck = millis();
            time_t now = time(nullptr);
            if (now > 1600000000) {
                int8_t tzOffset = Config::gps().timezoneOffset;
                now += (int32_t)tzOffset * 3600;
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                if ((timeinfo.tm_hour == 4 || timeinfo.tm_hour == 16) && timeinfo.tm_min == 20) {
                    bakedActive = true;
                    bakedStartMs = millis();
                    bakedDurationMs = random(120000, 420001);
                    bakedTriggered = true;
                }
            }
        }
    }

    // Update GPS
    if (Config::gps().enabled) {
        GPS::update();
    }

    // Update JanusHog coprocessor (non-blocking UART drain + state machine)
    JanusHog::update();

    // Update mood system
    Mood::update();

    // Update main controller (handles modes, input, state)
    porkchop.update();

    // Update display
    Display::update();

    // Slower update rate for smoother animation
}
