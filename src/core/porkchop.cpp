// Porkchop core state machine implementation

#include "porkchop.h"
#include "../ui/display.h"
#include "../ui/input.h"
#include "../ui/menu.h"
#include "../ui/settings_menu.h"
#include "../ui/hashes_menu.h"
#include "../ui/badges_menu.h"
#include "../ui/bounty_menu.h"
#include "../ui/diagdata_menu.h"
#include "../ui/flexes_screen.h"
#include "../ui/boar_bros_menu.h"
#include "../ui/tracks_menu.h"
#include "../ui/unlockables_menu.h"
#include "../ui/sd_format_menu.h"
#include "../piglet/mood.h"
#include "../piglet/avatar.h"
#include "../modes/oink.h"
#include "heap_policy.h"
#include "../modes/do_no_ham.h"
#include "../modes/warhog.h"
#include "../modes/piggy_blues.h"
#include "../modes/spectrum.h"
#include "../modes/pigsync_mode.h"
#include "../modes/bacon.h"
#include "janus_hog.h"
#include "../modes/charging.h"
#include "../web/xfer_server.h"
#include "../audio/sfx.h"
#include "../ui/haptic.h"
#include "config.h"
#include "heap_health.h"
#include "../piglet/narrative.h"
#include "xp.h"
#include "sdlog.h"
#include "sd_format.h"
#include "challenges.h"
#include "stress_test.h"
#include "network_recon.h"
#include "wifi_utils.h"
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <WiFi.h>

static const char* modeToString(PorkchopMode mode) {
    switch (mode) {
        case PorkchopMode::IDLE: return "IDLE";
        case PorkchopMode::OINK_MODE: return "OINK";
        case PorkchopMode::DNH_MODE: return "DNH";
        case PorkchopMode::WARHOG_MODE: return "WARHOG";
        case PorkchopMode::PIGGYBLUES_MODE: return "PIGGYBLUES";
        case PorkchopMode::SPECTRUM_MODE: return "SPECTRUM";
        case PorkchopMode::MENU: return "MENU";
        case PorkchopMode::SETTINGS: return "SETTINGS";
        case PorkchopMode::HASHES: return "HASHES";
        case PorkchopMode::BADGES: return "BADGES";
        case PorkchopMode::XFER: return "XFER";
        case PorkchopMode::DIAGDATA: return "DIAGDATA";
        case PorkchopMode::FLEXES: return "FLEXES";
        case PorkchopMode::BOAR_BROS: return "BOAR_BROS";
        case PorkchopMode::TRACKS: return "TRACKS";
        case PorkchopMode::UNLOCKABLES: return "UNLOCKABLES";
        case PorkchopMode::BOUNTY: return "BOUNTY";
        case PorkchopMode::PIGSYNC_DEVICE_SELECT: return "PIGSYNC_DEVICE_SELECT";
        case PorkchopMode::BACON_MODE: return "BACON";
        case PorkchopMode::JANUS_HOG_MODE: return "JANUS_HOG";
        case PorkchopMode::SD_FORMAT: return "SD_FORMAT";
        case PorkchopMode::CHARGING: return "CHARGING";
        case PorkchopMode::ABOUT: return "ABOUT";
        default: return "UNKNOWN";
    }
}

// Crash-loop guard: count early reboots using RTC memory (survives soft resets).
RTC_DATA_ATTR static uint8_t bootGuardStreak = 0;
static uint32_t bootGuardStartMs = 0;
static const uint8_t BOOT_GUARD_THRESHOLD = 3;
static const uint32_t BOOT_GUARD_WINDOW_MS = 60000;

// ML config saved before WARHOG disables it, restored on exit
static MLConfig savedMLConfig;

static PorkchopMode bootModeToPorkchop(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return PorkchopMode::OINK_MODE;
        case BootMode::DNOHAM: return PorkchopMode::DNH_MODE;
        case BootMode::WARHOG: return PorkchopMode::WARHOG_MODE;
        case BootMode::IDLE:
        default:
            return PorkchopMode::IDLE;
    }
}

static const char* bootModeLabel(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return "OINK";
        case BootMode::DNOHAM: return "DN0HAM";
        case BootMode::WARHOG: return "WARHOG";
        case BootMode::IDLE:
        default:
            return "IDLE";
    }
}


Porkchop::Porkchop()
    : currentMode(PorkchopMode::IDLE)
    , previousMode(PorkchopMode::IDLE)
    , startTime(0)
    , handshakeCount(0)
    , networkCount(0)
    , deauthCount(0) {
    eventQueue.reserve(MAX_EVENT_QUEUE_SIZE);
    callbacks.reserve(16);
}

static bool isAutoConditionSafe(PorkchopMode mode) {
    switch (mode) {
        case PorkchopMode::IDLE:
        case PorkchopMode::MENU:
        case PorkchopMode::SETTINGS:
        case PorkchopMode::ABOUT:
        case PorkchopMode::BADGES:
        case PorkchopMode::DIAGDATA:
        case PorkchopMode::FLEXES:
        case PorkchopMode::BOAR_BROS:
        case PorkchopMode::UNLOCKABLES:
        case PorkchopMode::BOUNTY:
        case PorkchopMode::SD_FORMAT:
            return true;
        default:
            return false;
    }
}

static void maybeAutoConditionHeap(PorkchopMode mode) {
    if (!isAutoConditionSafe(mode)) {
        return;
    }
    if (XferServer::isRunning() || XferServer::isConnecting()) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    // At Critical pressure (<30KB free), brew needs 35KB transient — would fail anyway
    if (static_cast<uint8_t>(HeapHealth::getPressureLevel()) > HeapPolicy::kMaxPressureLevelForAutoBrew) {
        return;
    }
    if (!HeapHealth::consumeConditionRequest()) {
        return;
    }

    bool wasReconRunning = NetworkRecon::isRunning();
    if (wasReconRunning) {
        NetworkRecon::pause();
    }
    // Small, low-disruption brew to coalesce heap when health drops.
    WiFiUtils::brewHeap(HeapPolicy::kBrewAutoDwellMs, false);
    if (wasReconRunning) {
        NetworkRecon::resume();
    }
}

void Porkchop::init() {
    startTime = millis();
    
    // Initialize background network reconnaissance service
    NetworkRecon::init();
    
    // Initialize XP system
    XP::init();
    
    // Initialize FlexesScreen (buff/debuff system)
    FlexesScreen::init();
    
    // Register level up callback to show popup
    XP::setLevelUpCallback([](uint8_t oldLevel, uint8_t newLevel) {
        Display::showLevelUp(oldLevel, newLevel);
        Avatar::cuteJump();  // Celebratory jump on level up!
        Mood::onLevelUp(newLevel);
        
        // Check if class tier changed (every 5 levels: 6, 11, 16, 21, 26, 31, 36)
        PorkClass oldClass = XP::getClassForLevel(oldLevel);
        PorkClass newClass = XP::getClassForLevel(newLevel);
        if (newClass != oldClass) {
            // Small delay between popups
            delay(500);
            Display::showClassPromotion(
                XP::getClassNameFor(oldClass),
                XP::getClassNameFor(newClass)
            );
        }
    });
    
    // Register default event handlers
    registerCallback(PorkchopEvent::HANDSHAKE_CAPTURED, [this](PorkchopEvent, void*) {
        handshakeCount++;
    });
    
    registerCallback(PorkchopEvent::NETWORK_FOUND, [this](PorkchopEvent, void*) {
        networkCount++;
    });
    
    registerCallback(PorkchopEvent::DEAUTH_SENT, [this](PorkchopEvent, void*) {
        deauthCount++;
    });
    
    // Menu selection handler - items now defined in menu.cpp as static arrays
    Menu::setCallback([this](uint8_t actionId) {
        switch (actionId) {
            case 1: setMode(PorkchopMode::OINK_MODE); break;
            case 2: setMode(PorkchopMode::WARHOG_MODE); break;
            case 3: setMode(PorkchopMode::XFER); break;
            case 4: setMode(PorkchopMode::HASHES); break;
            case 5: setMode(PorkchopMode::SETTINGS); break;
            case 6: setMode(PorkchopMode::ABOUT); break;
            case 8: setMode(PorkchopMode::PIGGYBLUES_MODE); break;
            case 9: setMode(PorkchopMode::BADGES); break;
            case 10: setMode(PorkchopMode::SPECTRUM_MODE); break;
            case 11: setMode(PorkchopMode::FLEXES); break;
            case 12: setMode(PorkchopMode::BOAR_BROS); break;
            case 13: setMode(PorkchopMode::TRACKS); break;
            case 14: setMode(PorkchopMode::DNH_MODE); break;
            case 15: setMode(PorkchopMode::UNLOCKABLES); break;
            case 16: setMode(PorkchopMode::PIGSYNC_DEVICE_SELECT); break;
            case 17: setMode(PorkchopMode::BOUNTY); break;
            case 18: setMode(PorkchopMode::BACON_MODE); break;
            case 19: setMode(PorkchopMode::DIAGDATA); break;
            case 20: setMode(PorkchopMode::SD_FORMAT); break;
            case 21: setMode(PorkchopMode::CHARGING); break;
            case 22: setMode(PorkchopMode::JANUS_HOG_MODE); break;
            case 23: Display::showWiFiQR(); break;
        }
    });

    bootGuardStartMs = millis();
    if (bootGuardStreak < 255) {
        bootGuardStreak++;
    }
    bool bootGuardActive = bootGuardStreak >= BOOT_GUARD_THRESHOLD;

    BootMode bootMode = Config::personality().bootMode;
    bootModeTarget = bootModeToPorkchop(bootMode);
    if (bootModeTarget != PorkchopMode::IDLE && !bootGuardActive) {
        bootModePending = true;
        bootModeStartMs = millis();
        char buf[32];
        snprintf(buf, sizeof(buf), "BOOT -> %s IN 5S", bootModeLabel(bootMode));
        Display::showToast(buf, 5000);
    } else if (bootModeTarget != PorkchopMode::IDLE && bootGuardActive) {
        Display::showToast("BOOT GUARD - IDLE", 4000);
    }
    
    Avatar::setState(AvatarState::HAPPY);
    
    // SFX::init() already called in setup() for boot sound — don't re-init

    Serial.println("[PORKCHOP] Initialized");
    SDLog::log("PORK", "Initialized - LV%d %s", XP::getLevel(), XP::getTitle());
}

void Porkchop::update() {
    // Update background network reconnaissance (channel hopping, cleanup)
    NetworkRecon::update();
    
    processEvents();
    yield(); // Allow other tasks to run between operations
    handleInput();
    yield(); // Allow other tasks to run between operations
    
    if (bootGuardStreak > 0 && (millis() - bootGuardStartMs >= BOOT_GUARD_WINDOW_MS)) {
        bootGuardStreak = 0;
    }
    if (bootModePending) {
        if (currentMode != PorkchopMode::IDLE) {
            bootModePending = false;
        } else if (millis() - bootModeStartMs >= 5000) {
            bootModePending = false;
            setMode(bootModeTarget);
        }
    }
    updateMode();

    maybeAutoConditionHeap(currentMode);
    
    // Tick non-blocking audio + haptic engines
    SFX::update();
    Haptic::update();
    yield(); // Allow other tasks to run between operations
    
    // Process one queued achievement celebration (debounced)
    XP::processAchievementQueue();
    yield(); // Allow other tasks to run between operations
    
    // Stress test injection (if active)
    StressTest::update();
    yield(); // Allow other tasks to run between operations
    
    // Check for session time XP bonuses
    XP::updateSessionTime();
    yield(); // Allow other tasks to run between operations
}

void Porkchop::setMode(PorkchopMode mode) {
    if (mode == currentMode) return;
    
    // Store the mode we're leaving for cleanup
    PorkchopMode oldMode = currentMode;

    Serial.printf("[MODE] EXIT %s free=%u\n",
        modeToString(oldMode),
        (unsigned)ESP.getFreeHeap());
    
    // Save "real" modes as previous (not modal menus)
    // Exception: HASHES and TRACKS are saved as previousMode so OINK recovery returns to them
    if (currentMode != PorkchopMode::SETTINGS &&
        currentMode != PorkchopMode::ABOUT &&
        currentMode != PorkchopMode::BADGES &&
        currentMode != PorkchopMode::MENU &&
        currentMode != PorkchopMode::XFER &&
        currentMode != PorkchopMode::DIAGDATA &&
        currentMode != PorkchopMode::FLEXES &&
        currentMode != PorkchopMode::BOAR_BROS &&
        currentMode != PorkchopMode::BOUNTY &&
        currentMode != PorkchopMode::PIGSYNC_DEVICE_SELECT &&
        currentMode != PorkchopMode::UNLOCKABLES &&
        currentMode != PorkchopMode::SD_FORMAT) {
        previousMode = currentMode;
    }
    // ALSO save HASHES and TRACKS as return points from OINK recovery
    if (currentMode == PorkchopMode::HASHES ||
        currentMode == PorkchopMode::TRACKS) {
        previousMode = currentMode;
    }
    currentMode = mode;

    Serial.printf("[MODE] ENTER %s free=%u\n",
        modeToString(currentMode),
        (unsigned)ESP.getFreeHeap());
    
    // Cleanup the mode we're actually leaving (oldMode), not previousMode
    switch (oldMode) {
        case PorkchopMode::OINK_MODE:
            OinkMode::stop();
            break;
        case PorkchopMode::DNH_MODE:
            DoNoHamMode::stop();
            break;
        case PorkchopMode::WARHOG_MODE:
            Config::setML(savedMLConfig);  // Restore ML config disabled on entry
            WarhogMode::stop();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            PiggyBluesMode::stop();
            break;
        case PorkchopMode::SPECTRUM_MODE:
            SpectrumMode::stop();
            break;
        case PorkchopMode::MENU:
            Menu::hide();
            break;
        case PorkchopMode::SETTINGS:
            SettingsMenu::hide();
            break;
        case PorkchopMode::HASHES:
            HashesMenu::hide();
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::hide();
            break;
        case PorkchopMode::XFER:
            XferServer::stop();
            // Restart NetworkRecon after XFER to resume background scanning
            NetworkRecon::start();
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::hide();
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::hide();
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::hide();
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::hide();
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::hide();
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::hide();
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::hide();
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            PigSyncMode::stopDiscovery();
            PigSyncMode::stop();
            break;
        case PorkchopMode::BACON_MODE:
            BaconMode::stop();
            break;
        case PorkchopMode::JANUS_HOG_MODE:
            // Thin viewer mode — nothing to stop (service runs independently)
            break;
        case PorkchopMode::CHARGING:
            ChargingMode::stop();
            // CHARGING explicitly shuts down JanusHog to save power.
            // Re-init it on exit so JANUS HOG returns automatically if enabled.
            if (JanusHog::isEnabled() && JanusHog::getState() == C5State::OFF) {
                JanusHog::init();
            }
            break;
        default:
            break;
    }
    
    // Init new mode
    switch (currentMode) {
        case PorkchopMode::IDLE:
            Avatar::setState(AvatarState::NEUTRAL);
            Mood::onIdle();
            XP::save();  // Save XP when returning to idle
            SDLog::log("PORK", "Mode: IDLE");
            break;
        case PorkchopMode::OINK_MODE:
            Avatar::setState(AvatarState::HUNTING);
            Display::notify(NoticeKind::STATUS, "PROPER MAD ONE INNIT", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: OINK");
            OinkMode::start();
            break;
        case PorkchopMode::DNH_MODE:
            Avatar::setState(AvatarState::NEUTRAL);  // Calm, passive state
            SDLog::log("PORK", "Mode: DO NO HAM");
            DoNoHamMode::start();
            break;
        case PorkchopMode::WARHOG_MODE:
            Avatar::setState(AvatarState::EXCITED);
            Display::notify(NoticeKind::STATUS, "SNIFFING THE AIR", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: WARHOG");
            // Save ML config then disable for heap savings (restored on exit)
            {
                savedMLConfig = Config::ml();
                auto mlCfg = savedMLConfig;
                mlCfg.enabled = false;
                mlCfg.collectionMode = MLCollectionMode::BASIC;
                Config::setML(mlCfg);
            }
            WarhogMode::start();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            Avatar::setState(AvatarState::ANGRY);
            SDLog::log("PORK", "Mode: PIGGYBLUES");
            PiggyBluesMode::start();
            // If user aborted or BLE init failed, return to menu
            if (!PiggyBluesMode::isRunning()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::SPECTRUM_MODE:
            Avatar::setState(AvatarState::HUNTING);
            SDLog::log("PORK", "Mode: SPECTRUM");
            SpectrumMode::start();
            break;
        case PorkchopMode::MENU:
            Menu::show();
            break;
        case PorkchopMode::SETTINGS:
            SettingsMenu::show();
            break;
        case PorkchopMode::HASHES:
            HashesMenu::show();
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::show();
            break;
        case PorkchopMode::XFER:
            // Stop NetworkRecon and free its ~19KB network vector — XFER doesn't use it
            NetworkRecon::stop();
            NetworkRecon::freeNetworks();
            Avatar::setState(AvatarState::HAPPY);
            XferServer::start(Config::wifi().otaSSID, Config::wifi().otaPassword);
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::show();
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::show();
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::show();
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::show();
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::show();
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::show();
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::show();
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: PIGSYNC Device Select");
            PigSyncMode::start();
            PigSyncMode::startDiscovery();
            break;
        case PorkchopMode::BACON_MODE:
            Avatar::setState(AvatarState::HAPPY);
            SDLog::log("PORK", "Mode: BACON");
            BaconMode::init();
            BaconMode::start();
            break;
        case PorkchopMode::JANUS_HOG_MODE:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: JANUS_HOG");
            if (JanusHog::isConnected()) {
                // Trigger initial scan when entering mode
                if (JanusHog::getScanCount() == 0) {
                    JanusHog::requestScan();
                }
            } else if (JanusHog::isEnabled()) {
                Display::notify(NoticeKind::STATUS, "C5 CONNECTING...", 2000, NoticeChannel::TOP_BAR);
            } else {
                Display::notify(NoticeKind::STATUS, "C5 DISABLED", 2000, NoticeChannel::TOP_BAR);
            }
            break;
        case PorkchopMode::ABOUT:
            Display::resetAboutState();
            break;
        case PorkchopMode::CHARGING:
            SDLog::log("PORK", "Mode: CHARGING");
            JanusHog::shutdown();  // Stop C5 to save power
            ChargingMode::start();
            break;
        default:
            break;
    }
    
    postEvent(PorkchopEvent::MODE_CHANGE, nullptr);
}

void Porkchop::postEvent(PorkchopEvent event, void* data) {
    // Prevent event queue overflow that could cause heap fragmentation
    if (eventQueue.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Drop oldest event to maintain queue size
        eventQueue.erase(eventQueue.begin());
    }
    eventQueue.push_back({event, data});
}

void Porkchop::registerCallback(PorkchopEvent event, EventCallback callback) {
    // Prevent duplicate callbacks for the same event to avoid multiple executions
    // Note: We can't reliably compare std::function objects, so we just ensure each event
    // type has only one callback by replacing any existing one
    for (auto& pair : callbacks) {
        if (pair.first == event) {
            pair.second = callback; // Replace existing callback
            return;
        }
    }
    // Add bounds checking to prevent unlimited growth
    if (callbacks.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Remove the oldest callback if we're at capacity
        callbacks.erase(callbacks.begin());
    }
    callbacks.push_back({event, callback});
}

void Porkchop::processEvents() {
    // Process events with bounds checking and yield for WDT safety
    // NOTE: All postEvent() callers pass nullptr for data — no ownership to track.
    size_t processed = 0;
    const size_t MAX_EVENTS_PER_UPDATE = 16; // Limit events processed per update to prevent WDT

    // Index-based loop to avoid iterator invalidation from erase()
    size_t i = 0;
    while (i < eventQueue.size() && processed < MAX_EVENTS_PER_UPDATE) {
        const auto item = eventQueue[i];  // Copy, not reference: callbacks may push_back() which reallocates

        for (const auto& cb : callbacks) {
            if (cb.first == item.event) {
                cb.second(item.event, item.data);

                if (++processed % 4 == 0) {
                    yield();
                }
            }
        }
        i++;
    }

    // Erase all processed events in one operation after the loop
    if (i >= eventQueue.size()) {
        eventQueue.clear();
    } else {
        eventQueue.erase(eventQueue.begin(), eventQueue.begin() + i);
    }
}

void Porkchop::handleInput() {
    // Global: power button short press toggles sound mute
    if (Input::powerShort()) {
        Display::resetDimTimer();
        bool& snd = Config::personality().soundEnabled;
        snd = !snd;
        if (!snd) M5.Speaker.stop();
        Display::showToast(snd ? "SOUND ON" : "SOUND OFF", 1000);
        Config::save();
        return;
    }

    // Global: screenshot (BtnC hold)
    if (Input::screenshot()) {
        Display::resetDimTimer();
        if (!Display::isSnapping()) {
            Display::takeScreenshot();
        }
        return;
    }

    // MENU: BtnB-hold closes modal, otherwise exits to IDLE.
    if (currentMode == PorkchopMode::MENU) {
        if (Input::back()) {
            Display::resetDimTimer();
            if (Menu::closeModal()) {
                return;
            }
            setMode(PorkchopMode::IDLE);
            return;
        }
        return;  // Menu consumes Up/Down/Select in its own update().
    }

    // ABOUT: BtnB-hold returns to MENU; BtnB-click triggers the easter egg.
    if (currentMode == PorkchopMode::ABOUT) {
        if (Input::back()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::MENU);
            return;
        }
        if (Input::select()) {
            Display::resetDimTimer();
            Display::onAboutEnterPressed();
            return;
        }
        return;
    }

    // SETTINGS: allow text-edit overlays to consume back-hold first.
    if (currentMode == PorkchopMode::SETTINGS) {
        if (!SettingsMenu::isTextEditing() && Input::back()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::MENU);
            return;
        }
        return;
    }

    // UNLOCKABLES: allow phrase entry overlay to consume back-hold first.
    if (currentMode == PorkchopMode::UNLOCKABLES) {
        if (!UnlockablesMenu::isTextEditing() && Input::back()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::MENU);
            return;
        }
        return;
    }

    // Other UI screens: back-hold returns to MENU.
    switch (currentMode) {
        case PorkchopMode::HASHES:
        case PorkchopMode::BADGES:
        case PorkchopMode::DIAGDATA:
        case PorkchopMode::FLEXES:
        case PorkchopMode::BOAR_BROS:
        case PorkchopMode::TRACKS:
        case PorkchopMode::BOUNTY:
        case PorkchopMode::SD_FORMAT:
            if (Input::back()) {
                Display::resetDimTimer();
                setMode(PorkchopMode::MENU);
                return;
            }
            return;
        default:
            break;
    }

    // PigSync device select (button-only)
    if (currentMode == PorkchopMode::PIGSYNC_DEVICE_SELECT) {
        if (Input::back()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::MENU);
            return;
        }

        bool up = Input::up();
        bool down = Input::down();
        bool sel = Input::select();
        if (up || down || sel) {
            Display::resetDimTimer();
        }

        if (PigSyncMode::isConnected()) {
            if (up && PigSyncMode::isSyncing()) {
                PigSyncMode::abortSync();
            }
            if (down) {
                PigSyncMode::disconnect();
            }
            return;
        }

        const uint8_t deviceCount = PigSyncMode::getDeviceCount();
        if (deviceCount > 0) {
            if (up) {
                PigSyncMode::selectDevice(PigSyncMode::getSelectedIndex() > 0
                    ? PigSyncMode::getSelectedIndex() - 1
                    : deviceCount - 1);
            }
            if (down) {
                PigSyncMode::selectDevice((PigSyncMode::getSelectedIndex() + 1) % deviceCount);
            }
            if (sel) {
                uint8_t selectedIdx = PigSyncMode::getSelectedIndex();
                if (selectedIdx < deviceCount) {
                    PigSyncMode::connectTo(selectedIdx);
                }
            }
        } else {
            if (sel && !PigSyncMode::isScanning()) {
                PigSyncMode::startScan();
            }
        }
        return;
    }

    // --- Expanded narrator: any input collapses (IDLE + active avatar modes) ---
    if (isAvatarMode(currentMode) && Display::isNarratorExpanded()) {
        Input::TapEvent t;
        if (Input::narratorTap() || Input::tap(t) || Input::select() ||
            Input::up() || Input::down() || Input::back() ||
            Input::swipeLeft() || Input::swipeRight() ||
            Input::swipeUp() || Input::swipeDown() || Input::doubleClick()) {
            Display::toggleNarratorExpanded();
            Display::resetDimTimer();
        }
        return;  // Consume the frame
    }

    // IDLE: BtnB double-click opens spectrum, BtnB click opens menu.
    if (currentMode == PorkchopMode::IDLE) {
        // Narrator tap toggle (before select check)
        if (Input::narratorTap()) {
            Display::toggleNarratorExpanded();
            Display::resetDimTimer();
            return;
        }
        if (Input::doubleClick()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::SPECTRUM_MODE);
            return;
        }
        if (Input::select()) {
            Display::resetDimTimer();
            setMode(PorkchopMode::MENU);
            return;
        }
        // Tap-to-pet: tap on avatar area triggers cuteJump + mood boost
        Input::TapEvent tapEv;
        if (Input::tap(tapEv)) {
            static uint32_t lastPetMs = 0;
            uint32_t now = millis();
            if (now - lastPetMs > 5000) {  // 5s cooldown
                lastPetMs = now;
                Avatar::cuteJump();
                Mood::adjustHappiness(3);
            }
        }
        return;
    }

    // Active avatar modes: narrator tap toggle
    if (isAvatarMode && Input::narratorTap()) {
        Display::toggleNarratorExpanded();
        Display::resetDimTimer();
        return;
    }

    // Active modes: BtnB-hold exits to IDLE.
    if (Input::back()) {
        Display::resetDimTimer();
        setMode(PorkchopMode::IDLE);
        return;
    }

    return;
}

void Porkchop::updateMode() {
    switch (currentMode) {
        case PorkchopMode::OINK_MODE:
            OinkMode::update();
            break;
        case PorkchopMode::DNH_MODE:
            DoNoHamMode::update();
            break;
        case PorkchopMode::WARHOG_MODE:
            WarhogMode::update();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            PiggyBluesMode::update();
            break;
        case PorkchopMode::SPECTRUM_MODE:
            SpectrumMode::update();
            break;
        case PorkchopMode::BACON_MODE:
            BaconMode::update();
            // Check if user exited
            if (!BaconMode::isRunning()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::HASHES:
            HashesMenu::update();
            if (!HashesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::update();
            if (!BadgesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::XFER:
            XferServer::update();
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::update();
            if (!DiagDataMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::update();
            if (!SdFormatMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::update();
            if (!FlexesScreen::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::update();
            if (!BoarBrosMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::update();
            if (!TracksMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::update();
            if (!UnlockablesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::update();
            if (!BountyMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            // Update PigSync discovery process (includes dialogue phases)
            PigSyncMode::update();
            // Stay in device select mode for terminal display
            if (!PigSyncMode::isRunning()) {
                // User exited, go back to menu
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::JANUS_HOG_MODE: {
            // Core2: map A/B/C to common Janus Hog actions.
            bool up = Input::up();
            bool sel = Input::select();
            bool down = Input::down();
            if (up || sel || down) {
                Display::resetDimTimer();
            }

            if (up) {
                if (JanusHog::isConnected()) {
                    JanusHog::requestScan();
                    Display::notify(NoticeKind::STATUS, "C5 SCAN STARTED", 1500, NoticeChannel::TOP_BAR);
                } else {
                    Display::notify(NoticeKind::STATUS, "C5 NOT CONNECTED", 1500, NoticeChannel::TOP_BAR);
                }
            } else if (sel) {
                if (JanusHog::isConnected()) {
                    JanusHog::requestChannelView();
                    Display::notify(NoticeKind::STATUS, "CH VIEW STARTED", 1500, NoticeChannel::TOP_BAR);
                } else {
                    Display::notify(NoticeKind::STATUS, "C5 NOT CONNECTED", 1500, NoticeChannel::TOP_BAR);
                }
            } else if (down) {
                if (JanusHog::isConnected()) {
                    if (JanusHog::requestImportNewestHandshake()) {
                        Display::notify(NoticeKind::STATUS, "IMPORT STARTED", 1500, NoticeChannel::TOP_BAR);
                    }
                } else {
                    Display::notify(NoticeKind::STATUS, "C5 NOT CONNECTED", 1500, NoticeChannel::TOP_BAR);
                }
            }
            break;
        }
        case PorkchopMode::CHARGING:
            ChargingMode::update();
            if (ChargingMode::shouldExit()) {
                setMode(PorkchopMode::IDLE);
            }
            break;
        default:
            break;
    }
}

uint32_t Porkchop::getUptime() const {
    return (millis() - startTime) / 1000;
}

uint16_t Porkchop::getHandshakeCount() const {
    // Include both handshakes and PMKIDs - both are crackable captures
    return OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
}

uint16_t Porkchop::getNetworkCount() const {
    return OinkMode::getNetworkCount();
}

uint16_t Porkchop::getDeauthCount() const {
    return OinkMode::getDeauthCount();
}
