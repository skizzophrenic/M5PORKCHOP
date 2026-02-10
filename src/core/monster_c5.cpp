// MonsterC5 - ESP32-C5 Coprocessor Service (JANUS HOG)
// UART bridge to JanOS/projectZero running on MonsterC5 board.
// Non-blocking, zero dynamic allocations, max 128 UART bytes per update().

#include "monster_c5.h"
#include "config.h"
#include "xp.h"
#include "heap_gates.h"
#include "network_recon.h"
#include "../gps/gps.h"
#include "../piglet/mood.h"
#include "../piglet/avatar.h"
#include "../ui/display.h"
#include <string.h>

// NOTE: Per user request, JANUS HOG uses SERIAL logging only (no SDLog).
#define C5_LOGF(fmt, ...) Serial.printf("[C5] " fmt "\n", ##__VA_ARGS__)

// ============================================================================
// Internal State
// ============================================================================

// Command sequencer steps (multi-step JanOS operations)
enum class SeqStep : uint8_t {
    IDLE,
    STOPPING,           // Sent "stop", waiting for quiet
    SCAN_REQUESTED,     // Sent "scan_networks"
    SCAN_SHOW_RESULTS,  // Sent "show_scan_results"
    TARGET_SELECTING,   // Matching BSSID in C5 scan results
    SAE_SELECT_SENT,    // Sent "select_networks", waiting for confirmation
    ATTACK_SENT,        // Sent "start_handshake <idx>"
    ATTACK_MONITORING,  // Parsing progress
    CHANNEL_VIEW_ACTIVE,
    PACKET_MONITOR_ACTIVE,
    DONE
};

// All static state — zero heap allocations
static C5State          state = C5State::OFF;
static C5Op             currentOp = C5Op::NONE;
static SeqStep          seqStep = SeqStep::IDLE;
static HandshakeResult  hsResult = HandshakeResult::IDLE;

// UART line buffer
static char     lineBuf[1024];  // JanOS line buffer is 1024B (see docs/C5_UART_INTEGRATION_SPEC.txt)
static uint16_t linePos = 0;
static bool     lineOverflow = false;  // Drop oversize lines safely until newline

// Scan cache
static constexpr uint8_t SCAN_CACHE_MAX = 128;
// Double-buffered: keep last completed scan visible during an active scan.
// This prevents Spectrum's 5GHz overlay from flickering to "no data" between scans.
static C5ScanEntry  scanCacheA[SCAN_CACHE_MAX];
static C5ScanEntry  scanCacheB[SCAN_CACHE_MAX];
static C5ScanEntry* scanCacheActive = scanCacheA;
static C5ScanEntry* scanCacheWork = scanCacheB;
static uint8_t      scanCountActive = 0;
static uint8_t      scanCountWork = 0;
static bool         scanWork5GHzXpAwarded = false;  // Once per scan batch
static uint32_t     lastScanCompleteMs = 0;

// Channel counts
static C5ChannelCounts channelCounts;
static bool            parsingChannelView = false;
static bool            parsing5GHz = false;

// Packet monitor
static uint32_t pktPerSecond = 0;

// Target BSSID for sequenced operations
static uint8_t targetBssid[6];
static int8_t  targetC5Index = -1;     // 1-based C5 scan index

// Timing
static uint32_t lastPingMs = 0;
static uint32_t lastRxMs = 0;
static uint32_t stateEnteredMs = 0;
static uint32_t seqStepEnteredMs = 0;

// Connection lifecycle
static uint8_t  pingAttempts = 0;
static uint8_t  errorRetries = 0;
static uint32_t errorBackoffMs = 5000;
static bool     c5XpAwarded = false;    // Once per session
static bool     gpsPaused = false;

// Auto-scan timer (uses C5Config::scanIntervalMs, 0 = disabled)
static uint32_t lastAutoScanMs = 0;
static bool     firstScanDone = false;

// Protocol timing constants
static constexpr uint32_t PING_INTERVAL_MS      = 2000;
static constexpr uint32_t DETECT_TIMEOUT_MS     = 30000;
static constexpr uint32_t SCAN_TIMEOUT_MS       = 20000;
static constexpr uint32_t ATTACK_TIMEOUT_MS     = 60000;
static constexpr uint32_t STOP_QUIET_MS         = 500;
static constexpr uint32_t HEARTBEAT_STALE_MS    = 15000;
static constexpr uint32_t HEARTBEAT_PROBE_MS    = 3000;
static constexpr uint8_t  MAX_READ_PER_UPDATE   = 128;
static constexpr uint32_t MAX_ERROR_BACKOFF_MS  = 30000;

// ============================================================================
// Forward Declarations
// ============================================================================

static void sendCommand(const char* cmd);
static void processLine(const char* line);
static void parseScanCSV(const char* line);
static void parseChannelViewLine(const char* line);
static void setState(C5State newState);
static void setSeqStep(SeqStep step);
static void advanceSequencer();
static void injectScanIntoRecon();
static wifi_auth_mode_t mapAuthString(const char* auth);
static int findBssidInCache(const uint8_t* bssid);

// ============================================================================
// 5GHz Channel Mapping
// ============================================================================

static const uint8_t CH5_MAP[] = {
    36, 40, 44, 48, 52, 56, 60, 64,
    100, 104, 108, 112, 116, 120, 124, 128,
    132, 136, 140, 144, 149, 153, 157, 161, 165
};
static constexpr uint8_t CH5_COUNT = sizeof(CH5_MAP);

static int8_t ch5ToIndex(uint8_t channel) {
    for (uint8_t i = 0; i < CH5_COUNT; i++) {
        if (CH5_MAP[i] == channel) return i;
    }
    return -1;
}

// ============================================================================
// Lifecycle
// ============================================================================

void MonsterC5::init() {
    state = C5State::OFF;
    currentOp = C5Op::NONE;
    seqStep = SeqStep::IDLE;
    hsResult = HandshakeResult::IDLE;
    linePos = 0;
    scanCountActive = 0;
    scanCountWork = 0;
    scanWork5GHzXpAwarded = false;
    pingAttempts = 0;
    errorRetries = 0;
    errorBackoffMs = 5000;
    c5XpAwarded = false;
    gpsPaused = false;
    pktPerSecond = 0;
    memset(scanCacheA, 0, sizeof(scanCacheA));
    memset(scanCacheB, 0, sizeof(scanCacheB));
    scanCacheActive = scanCacheA;
    scanCacheWork = scanCacheB;
    memset(&channelCounts, 0, sizeof(channelCounts));
    memset(lineBuf, 0, sizeof(lineBuf));
    memset(targetBssid, 0, sizeof(targetBssid));
    targetC5Index = -1;
    parsingChannelView = false;
    parsing5GHz = false;
    lastAutoScanMs = 0;
    firstScanDone = false;
    lineOverflow = false;
    lastScanCompleteMs = 0;

    if (!Config::c5().enabled) {
        return;
    }

    // Heap gate check
    auto gate = HeapGates::checkGate(20000, 15000);
    if (!HeapGates::canMeet(gate, nullptr, 0)) {
        C5_LOGF("Heap too low for C5 init");
        return;
    }

    // Handle GPS pin conflict — check any overlap, not just Grove defaults
    GPSConfig gpsCfg = Config::gps();
    uint8_t txPin = Config::c5().uartTxPin;
    uint8_t rxPin = Config::c5().uartRxPin;

    if (gpsCfg.enabled &&
        (txPin == gpsCfg.txPin || txPin == gpsCfg.rxPin ||
         rxPin == gpsCfg.txPin || rxPin == gpsCfg.rxPin)) {
        GPS::sleep();
        gpsPaused = true;
        C5_LOGF("GPS sleeping - C5 pins overlap GPS (TX%d/RX%d vs G%d/G%d)",
                   txPin, rxPin, gpsCfg.txPin, gpsCfg.rxPin);
    }

    Serial1.begin(Config::c5().baudRate, SERIAL_8N1, rxPin, txPin);

    setState(C5State::DISCONNECTED);
    lastPingMs = millis();
    sendCommand("ping");

    C5_LOGF("Init (RX=%d TX=%d baud=%lu)",
               rxPin, txPin, Config::c5().baudRate);
}

void MonsterC5::update() {
    if (state == C5State::OFF) return;

    uint32_t now = millis();

    // --- UART RX: drain bytes into lineBuf, process complete lines ---
    uint8_t bytesRead = 0;
    while (Serial1.available() && bytesRead < MAX_READ_PER_UPDATE) {
        char c = (char)Serial1.read();
        bytesRead++;
        lastRxMs = now;

        if (c == '\n' || c == '\r') {
            if (linePos > 0 && !lineOverflow) {
                lineBuf[linePos] = '\0';
                processLine(lineBuf);
            }
            linePos = 0;
            lineOverflow = false;
        } else if (linePos < sizeof(lineBuf) - 1) {
            if (!lineOverflow) {
                lineBuf[linePos++] = c;
            }
        } else {
            // Oversize line: stop buffering until newline to avoid truncated parses.
            lineOverflow = true;
        }

        if (bytesRead % 32 == 0) yield();
    }

    // --- Connection state machine ---
    switch (state) {
        case C5State::DISCONNECTED:
            if (now - lastPingMs >= PING_INTERVAL_MS) {
                sendCommand("ping");
                lastPingMs = now;
                pingAttempts++;
            }
            if (now - stateEnteredMs >= DETECT_TIMEOUT_MS) {
                setState(C5State::ERROR);
                C5_LOGF("Detection timeout after %d pings", pingAttempts);
            }
            break;

        case C5State::CONNECTED:
            // Heartbeat: if no data for HEARTBEAT_STALE_MS, probe
            if (now - lastRxMs >= HEARTBEAT_STALE_MS) {
                sendCommand("ping");
                lastPingMs = now;
                if (now - lastRxMs >= HEARTBEAT_STALE_MS + HEARTBEAT_PROBE_MS) {
                    C5_LOGF("Heartbeat lost, reconnecting");
                    setState(C5State::DISCONNECTED);
                }
            }
            // Auto-scan timer: periodic 5GHz network discovery
            {
                uint16_t interval = Config::c5().scanIntervalMs;
                if (interval > 0 && currentOp == C5Op::NONE) {
                    if (!firstScanDone || (now - lastAutoScanMs >= interval)) {
                        lastAutoScanMs = now;
                        firstScanDone = true;
                        requestScan();
                    }
                }
            }
            break;

        case C5State::SCANNING: {
            // Handle scan timeout (but not after we already got the completion marker)
            bool scanInProgress = (seqStep == SeqStep::STOPPING ||
                                   seqStep == SeqStep::SCAN_REQUESTED ||
                                   seqStep == SeqStep::SCAN_SHOW_RESULTS);
            if (scanInProgress && (now - stateEnteredMs >= SCAN_TIMEOUT_MS)) {
                C5_LOGF("Scan timeout, %d networks parsed", scanCountWork);

                // If we have partial results, finalize them so UI/recon stays consistent.
                if (scanCountWork > 0) {
                    // Swap: make the (partial) work cache the active cache.
                    C5ScanEntry* oldActive = scanCacheActive;
                    scanCacheActive = scanCacheWork;
                    scanCacheWork = oldActive;
                    scanCountActive = scanCountWork;
                    scanCountWork = 0;
                    scanWork5GHzXpAwarded = false;
                    lastScanCompleteMs = now;
                    injectScanIntoRecon();

                    // If this scan was servicing a handshake, still attempt target selection.
                    if (currentOp == C5Op::HANDSHAKE || currentOp == C5Op::SAE_OVERFLOW) {
                        setSeqStep(SeqStep::TARGET_SELECTING);
                        break;
                    }
                }

                if (currentOp == C5Op::HANDSHAKE) {
                    // Scan timed out while trying to service a handshake request.
                    hsResult = HandshakeResult::FAILED;
                }
                setState(C5State::CONNECTED);
                currentOp = C5Op::NONE;
                setSeqStep(SeqStep::IDLE);
            }
            // Sequencer may need to advance
            advanceSequencer();
            break;
        }

        case C5State::ATTACKING:
            if (now - stateEnteredMs >= ATTACK_TIMEOUT_MS) {
                C5_LOGF("Attack timeout");
                hsResult = HandshakeResult::FAILED;
                sendCommand("stop");
                setState(C5State::CONNECTED);
                currentOp = C5Op::NONE;
                setSeqStep(SeqStep::IDLE);
            }
            advanceSequencer();
            break;

        case C5State::MONITORING:
            // channel_view/packet_monitor run until requestStop()
            // Heartbeat still applies
            if (now - lastRxMs >= HEARTBEAT_STALE_MS + HEARTBEAT_PROBE_MS) {
                C5_LOGF("Monitor heartbeat lost");
                setState(C5State::DISCONNECTED);
            }
            break;

        case C5State::ERROR:
            if (now - stateEnteredMs >= errorBackoffMs) {
                errorRetries++;
                errorBackoffMs = errorBackoffMs * 2;
                if (errorBackoffMs > MAX_ERROR_BACKOFF_MS) {
                    errorBackoffMs = MAX_ERROR_BACKOFF_MS;
                }
                setState(C5State::DISCONNECTED);
                pingAttempts = 0;
            }
            break;

        default:
            break;
    }
}

void MonsterC5::shutdown() {
    if (state == C5State::OFF) return;

    sendCommand("stop");
    delay(50);
    Serial1.end();

    if (gpsPaused) {
        GPS::wake();
        gpsPaused = false;
    }

    state = C5State::OFF;
    currentOp = C5Op::NONE;
    seqStep = SeqStep::IDLE;
    scanCountActive = 0;
    scanCountWork = 0;
    scanWork5GHzXpAwarded = false;
    lastScanCompleteMs = 0;

    C5_LOGF("Shutdown");
}

void MonsterC5::reinit() {
    shutdown();
    delay(50);
    init();
}

// ============================================================================
// State Queries
// ============================================================================

C5State MonsterC5::getState() { return state; }
C5Op MonsterC5::getCurrentOp() { return currentOp; }
bool MonsterC5::isEnabled() { return Config::c5().enabled; }

bool MonsterC5::isConnected() {
    return state == C5State::CONNECTED ||
           state == C5State::SCANNING ||
           state == C5State::ATTACKING ||
           state == C5State::MONITORING;
}

// ============================================================================
// Active Commands
// ============================================================================

bool MonsterC5::requestScan() {
    if (!isConnected()) return false;

    scanCountWork = 0;
    scanWork5GHzXpAwarded = false;
    lastAutoScanMs = millis();  // Reset auto-scan timer on manual scan too
    currentOp = C5Op::SCAN;

    if (state != C5State::CONNECTED) {
        // Something else is running — go through STOPPING sequence
        setState(C5State::SCANNING);
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
    } else {
        setState(C5State::SCANNING);
        setSeqStep(SeqStep::SCAN_REQUESTED);
        sendCommand("scan_networks");
    }
    C5_LOGF("Scan requested");
    return true;
}

bool MonsterC5::requestHandshake(const uint8_t* bssid) {
    if (!isConnected()) return false;

    memcpy(targetBssid, bssid, 6);
    targetC5Index = -1;
    hsResult = HandshakeResult::IN_PROGRESS;
    currentOp = C5Op::HANDSHAKE;

    // Check if target is in current scan cache
    int idx = findBssidInCache(bssid);
    if (idx >= 0) {
        // Direct attack — skip rescan
        targetC5Index = scanCacheActive[idx].c5Index;
        setState(C5State::ATTACKING);
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
        C5_LOGF("Handshake request (cached idx=%d)", targetC5Index);
    } else {
        // Need to rescan to find BSSID
        scanCountWork = 0;
        scanWork5GHzXpAwarded = false;
        setState(C5State::SCANNING);
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
        C5_LOGF("Handshake request (need rescan)");
    }
    return true;
}

bool MonsterC5::requestSaeOverflow(const uint8_t* bssid) {
    if (!isConnected()) return false;

    memcpy(targetBssid, bssid, 6);
    targetC5Index = -1;
    currentOp = C5Op::SAE_OVERFLOW;

    int idx = findBssidInCache(bssid);
    if (idx >= 0) {
        targetC5Index = scanCacheActive[idx].c5Index;
        setState(C5State::ATTACKING);
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
    } else {
        scanCountWork = 0;
        scanWork5GHzXpAwarded = false;
        setState(C5State::SCANNING);
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
    }
    return true;
}

bool MonsterC5::requestChannelView() {
    if (!isConnected()) return false;

    currentOp = C5Op::CHANNEL_VIEW;
    memset(&channelCounts, 0, sizeof(channelCounts));
    parsingChannelView = false;
    parsing5GHz = false;

    if (state != C5State::CONNECTED) {
        // Something else is running — go through STOPPING sequence
        setSeqStep(SeqStep::STOPPING);
        sendCommand("stop");
    } else {
        setState(C5State::MONITORING);
        setSeqStep(SeqStep::CHANNEL_VIEW_ACTIVE);
        sendCommand("channel_view");
    }
    C5_LOGF("Channel view requested");
    return true;
}

bool MonsterC5::requestPacketMonitor(uint8_t channel) {
    if (!isConnected()) return false;

    currentOp = C5Op::PACKET_MONITOR;
    setState(C5State::MONITORING);
    setSeqStep(SeqStep::PACKET_MONITOR_ACTIVE);
    pktPerSecond = 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "packet_monitor %d", channel);
    sendCommand(cmd);
    C5_LOGF("Packet monitor ch%d requested", channel);
    return true;
}

void MonsterC5::requestStop() {
    if (state == C5State::OFF || state == C5State::DISCONNECTED) return;
    sendCommand("stop");
    currentOp = C5Op::NONE;
    seqStep = SeqStep::IDLE;
    parsingChannelView = false;
    if (state != C5State::ERROR) {
        setState(C5State::CONNECTED);
    }
}

// ============================================================================
// Result Polling
// ============================================================================

HandshakeResult MonsterC5::getHandshakeResult() { return hsResult; }
void MonsterC5::clearHandshakeResult() { hsResult = HandshakeResult::IDLE; }
const C5ChannelCounts& MonsterC5::getChannelCounts() { return channelCounts; }
uint32_t MonsterC5::getPacketsPerSecond() { return pktPerSecond; }
uint8_t MonsterC5::getScanCount() { return scanCountActive; }

const C5ScanEntry* MonsterC5::getScanEntry(uint8_t index) {
    if (index >= scanCountActive) return nullptr;
    return &scanCacheActive[index];
}

// ============================================================================
// Status String
// ============================================================================

void MonsterC5::getStatusString(char* buf, uint8_t len) {
    const char* stateStr = "OFF";
    switch (state) {
        case C5State::OFF:          stateStr = "OFF"; break;
        case C5State::DISCONNECTED: stateStr = "SEARCHING"; break;
        case C5State::CONNECTED:    stateStr = "READY"; break;
        case C5State::SCANNING:     stateStr = "SCANNING"; break;
        case C5State::ATTACKING:    stateStr = "ATTACKING"; break;
        case C5State::MONITORING:   stateStr = "MONITORING"; break;
        case C5State::ERROR:        stateStr = "ERROR"; break;
    }
    snprintf(buf, len, "C5:%s", stateStr);
}

// ============================================================================
// Internal: UART TX
// ============================================================================

static void sendCommand(const char* cmd) {
    if (!cmd || cmd[0] == '\0') return;
    Serial1.print(cmd);
    Serial1.print('\n');
    Serial1.flush();

    // Avoid spamming Serial with periodic keep-alives.
    if (strcmp(cmd, "ping") != 0) {
        C5_LOGF("TX> %s", cmd);
    }
}

// ============================================================================
// Internal: State Helpers
// ============================================================================

static void setState(C5State newState) {
    state = newState;
    stateEnteredMs = millis();
    if (newState == C5State::DISCONNECTED) {
        pingAttempts = 0;
    }
    if (newState == C5State::CONNECTED) {
        errorRetries = 0;
        errorBackoffMs = 5000;
    }
}

static void setSeqStep(SeqStep step) {
    seqStep = step;
    seqStepEnteredMs = millis();
}

// ============================================================================
// Internal: Line Processing
// ============================================================================

static void processLine(const char* line) {
    if (!line || line[0] == '\0') return;

    // --- Pong (connection handshake) ---
    if (strcmp(line, "pong") == 0) {
        if (state == C5State::DISCONNECTED || state == C5State::ERROR) {
            setState(C5State::CONNECTED);
            Display::notify(NoticeKind::STATUS, "C5 BOARD LINKED", 2000, NoticeChannel::TOP_BAR);
            Mood::setStatusMessage("5ghz linked up");
            Avatar::cuteJump();

            if (!c5XpAwarded) {
                XP::addXP(XPEvent::C5_CONNECTED);
                c5XpAwarded = true;
            }
            C5_LOGF("Board connected");
        }
        return;
    }

    // --- Scan completion marker ---
    if (strncmp(line, "Scan results printed", 20) == 0) {
        if (state == C5State::SCANNING) {
            C5_LOGF("Scan complete: %d networks", scanCountWork);

            // Finalize scan results: swap work cache into active so UI can keep showing data while next scan runs.
            C5ScanEntry* oldActive = scanCacheActive;
            scanCacheActive = scanCacheWork;
            scanCacheWork = oldActive;
            scanCountActive = scanCountWork;
            scanCountWork = 0;
            scanWork5GHzXpAwarded = false;
            lastScanCompleteMs = millis();

            injectScanIntoRecon();

            // If we were scanning as part of a handshake sequence, advance
            if (currentOp == C5Op::HANDSHAKE || currentOp == C5Op::SAE_OVERFLOW) {
                setSeqStep(SeqStep::TARGET_SELECTING);
            } else {
                currentOp = C5Op::NONE;
                setSeqStep(SeqStep::IDLE);
                setState(C5State::CONNECTED);

                char buf[32];
                snprintf(buf, sizeof(buf), "C5: %d NETS FOUND", scanCountActive);
                Display::notify(NoticeKind::STATUS, buf, 2000, NoticeChannel::TOP_BAR);
            }
        }
        return;
    }

    // --- Scan async status ---
    if (strncmp(line, "WiFi scan completed", 19) == 0) {
        if (seqStep == SeqStep::SCAN_REQUESTED) {
            // JanOS scan done, now request results
            setSeqStep(SeqStep::SCAN_SHOW_RESULTS);
            sendCommand("show_scan_results");
        }
        return;
    }

    if (strncmp(line, "No networks found", 17) == 0 ||
        strncmp(line, "No scan has been", 16) == 0) {
        if (state == C5State::SCANNING) {
            C5_LOGF("Scan: %s", line);
            scanCountWork = 0;
            scanWork5GHzXpAwarded = false;
            scanCountActive = 0;
            lastScanCompleteMs = millis();
            if (currentOp == C5Op::HANDSHAKE) {
                hsResult = HandshakeResult::FAILED;
            }
            setState(C5State::CONNECTED);
            currentOp = C5Op::NONE;
            setSeqStep(SeqStep::IDLE);
        }
        return;
    }

    // --- Handshake capture markers ---
    if (strstr(line, "Handshake captured for") != nullptr) {
        hsResult = HandshakeResult::CAPTURED;
        C5_LOGF("Handshake captured: %s", line);
        return;
    }

    if (strstr(line, "Handshake attack completed") != nullptr) {
        // If we never saw a capture marker, count as failure.
        if (hsResult == HandshakeResult::IN_PROGRESS) {
            hsResult = HandshakeResult::FAILED;
        }
        sendCommand("stop");
        setState(C5State::CONNECTED);
        currentOp = C5Op::NONE;
        setSeqStep(SeqStep::IDLE);
        return;
    }

    // --- Channel view parsing ---
    if (strcmp(line, "channel_view_start") == 0) {
        parsingChannelView = true;
        parsing5GHz = false;
        memset(&channelCounts, 0, sizeof(channelCounts));
        return;
    }
    if (strcmp(line, "channel_view_end") == 0) {
        parsingChannelView = false;
        channelCounts.valid = true;
        return;
    }
    if (parsingChannelView) {
        parseChannelViewLine(line);
        return;
    }

    // --- Packet monitor ---
    {
        const char* pktSuffix = strstr(line, "pkts");
        if (pktSuffix != nullptr && pktSuffix != line) {
            // Parse "<N>pkts"
            pktPerSecond = (uint32_t)atoi(line);
            return;
        }
    }

    // --- Stop confirmation ---
    if (strstr(line, "Stop command received") != nullptr ||
        strstr(line, "Channel view monitor stopped") != nullptr ||
        strstr(line, "Stopping packet monitor") != nullptr) {
        if (seqStep == SeqStep::STOPPING) {
            // Advance sequencer past stop phase
            advanceSequencer();
        }
        return;
    }

    // --- CSV scan lines (start with '"') ---
    if (line[0] == '"' && (state == C5State::SCANNING) &&
        (seqStep == SeqStep::SCAN_SHOW_RESULTS || seqStep == SeqStep::SCAN_REQUESTED)) {
        parseScanCSV(line);
        return;
    }

    // --- "Selected network N:" confirmation (for SAE overflow sequence) ---
    if (strncmp(line, "Selected network", 16) == 0) {
        if (seqStep == SeqStep::SAE_SELECT_SENT) {
            sendCommand("sae_overflow");
            setSeqStep(SeqStep::ATTACK_MONITORING);
            C5_LOGF("SAE overflow started after selection");
        }
        return;
    }

    // --- "Scan still in progress..." retry ---
    if (strncmp(line, "Scan still in progress", 22) == 0) {
        // JanOS hasn't finished scanning yet. Retry show_scan_results after 500ms.
        // Reset the seqStep timer so advanceSequencer doesn't time out prematurely.
        if (seqStep == SeqStep::SCAN_SHOW_RESULTS) {
            setSeqStep(SeqStep::SCAN_REQUESTED); // Will re-trigger show_scan_results on "WiFi scan completed"
        }
        return;
    }

    // --- Error messages ---
    if (strstr(line, "already active") != nullptr ||
        strstr(line, "already running") != nullptr) {
        C5_LOGF("Conflict: %s", line);
        return;
    }
}

// ============================================================================
// Internal: JanOS CSV Parser
// ============================================================================
// Format: "id","ssid","vendor","bssid","channel","security","rssi","band"
// All fields double-quoted. Parse by walking quotes — no strdup, no strtok.

static bool extractQuotedField(const char*& p, char* out, uint8_t maxLen) {
    if (*p != '"') return false;
    p++; // skip opening quote

    uint8_t i = 0;
    while (*p) {
        // CSV escape: "" inside a quoted field means a literal '"'
        if (*p == '"' && *(p + 1) == '"') {
            if (i < maxLen - 1) out[i++] = '"';
            p += 2;
            continue;
        }
        // Closing quote: '"' followed by comma or EOL
        if (*p == '"' && (*(p + 1) == ',' || *(p + 1) == '\0' || *(p + 1) == '\n')) {
            break;
        }
        if (i < maxLen - 1) out[i++] = *p;
        p++;
    }
    out[i] = '\0';

    if (*p == '"') p++; // skip closing quote
    if (*p == ',') p++; // skip comma

    return true;
}

static void parseScanCSV(const char* line) {
    if (scanCountWork >= SCAN_CACHE_MAX) return;

    char field[65]; // reusable field buffer
    const char* p = line;

    C5ScanEntry& entry = scanCacheWork[scanCountWork];
    memset(&entry, 0, sizeof(C5ScanEntry));

    // Field 0: index (1-based)
    if (!extractQuotedField(p, field, sizeof(field))) return;
    entry.c5Index = (uint8_t)atoi(field);

    // Field 1: SSID
    if (!extractQuotedField(p, entry.ssid, sizeof(entry.ssid))) return;

    // Field 2: vendor (ignore)
    if (!extractQuotedField(p, field, sizeof(field))) return;

    // Field 3: BSSID ("AA:BB:CC:DD:EE:FF")
    if (!extractQuotedField(p, field, sizeof(field))) return;
    if (strlen(field) >= 17) {
        for (int i = 0; i < 6; i++) {
            char hex[3] = { field[i * 3], field[i * 3 + 1], '\0' };
            entry.bssid[i] = (uint8_t)strtol(hex, nullptr, 16);
        }
    }

    // Field 4: channel
    if (!extractQuotedField(p, field, sizeof(field))) return;
    entry.channel = (uint8_t)atoi(field);

    // Field 5: security
    if (!extractQuotedField(p, field, sizeof(field))) return;
    entry.authmode = (uint8_t)mapAuthString(field);

    // Field 6: RSSI
    if (!extractQuotedField(p, field, sizeof(field))) return;
    entry.rssi = (int8_t)atoi(field);

    // Field 7: band (ignore — derived from channel)

    scanCountWork++;

    // Award XP once per scan batch for discovering any 5GHz networks.
    if (entry.channel > 14 && !scanWork5GHzXpAwarded) {
        scanWork5GHzXpAwarded = true;
        XP::addXP(XPEvent::C5_5GHZ_FOUND);
    }
}

static wifi_auth_mode_t mapAuthString(const char* auth) {
    if (strcmp(auth, "OPEN") == 0) return WIFI_AUTH_OPEN;
    if (strcmp(auth, "WEP") == 0) return WIFI_AUTH_WEP;
    if (strcmp(auth, "WPA_PSK") == 0) return WIFI_AUTH_WPA_PSK;
    if (strcmp(auth, "WPA2_PSK") == 0) return WIFI_AUTH_WPA2_PSK;
    if (strcmp(auth, "WPA_WPA2_PSK") == 0) return WIFI_AUTH_WPA_WPA2_PSK;
    if (strcmp(auth, "WPA2_ENTERPRISE") == 0) return WIFI_AUTH_WPA2_ENTERPRISE;
    if (strcmp(auth, "WPA3_PSK") == 0) return WIFI_AUTH_WPA3_PSK;
    if (strcmp(auth, "WPA2_WPA3_PSK") == 0) return WIFI_AUTH_WPA2_WPA3_PSK;
    if (strcmp(auth, "WAPI_PSK") == 0) return WIFI_AUTH_WAPI_PSK;
    return WIFI_AUTH_WPA2_PSK; // default
}

// ============================================================================
// Internal: Channel View Parser
// ============================================================================

static void parseChannelViewLine(const char* line) {
    if (strcmp(line, "band:24") == 0) {
        parsing5GHz = false;
        return;
    }
    if (strcmp(line, "band:5") == 0) {
        parsing5GHz = true;
        return;
    }

    // Parse "ch<N>:<count>"
    if (line[0] != 'c' || line[1] != 'h') return;

    const char* colonPos = strchr(line + 2, ':');
    if (!colonPos) return;

    int chNum = atoi(line + 2);
    uint16_t count = (uint16_t)atoi(colonPos + 1);

    if (!parsing5GHz) {
        if (chNum >= 1 && chNum <= 14) {
            channelCounts.ch24[chNum - 1] = count;
        }
    } else {
        int8_t idx = ch5ToIndex((uint8_t)chNum);
        if (idx >= 0) {
            channelCounts.ch5[idx] = count;
        }
    }
}

// ============================================================================
// Internal: Command Sequencer
// ============================================================================

static void advanceSequencer() {
    uint32_t elapsed = millis() - seqStepEnteredMs;

    switch (seqStep) {
        case SeqStep::STOPPING:
            if (elapsed >= STOP_QUIET_MS) {
                if (currentOp == C5Op::SCAN) {
                    setSeqStep(SeqStep::SCAN_REQUESTED);
                    sendCommand("scan_networks");
                } else if (currentOp == C5Op::HANDSHAKE || currentOp == C5Op::SAE_OVERFLOW) {
                    if (targetC5Index > 0) {
                        // Already have index, go straight to attack
                        char cmd[48];
                        if (currentOp == C5Op::HANDSHAKE) {
                            snprintf(cmd, sizeof(cmd), "start_handshake %d", targetC5Index);
                            sendCommand(cmd);
                            setSeqStep(SeqStep::ATTACK_SENT);
                        } else {
                            // SAE: select first, wait for confirm, then overflow
                            snprintf(cmd, sizeof(cmd), "select_networks %d", targetC5Index);
                            sendCommand(cmd);
                            setSeqStep(SeqStep::SAE_SELECT_SENT);
                        }
                        setState(C5State::ATTACKING);
                    } else {
                        // Need to scan first
                        scanCountWork = 0;
                        scanWork5GHzXpAwarded = false;
                        setSeqStep(SeqStep::SCAN_REQUESTED);
                        sendCommand("scan_networks");
                        setState(C5State::SCANNING);
                    }
                } else if (currentOp == C5Op::CHANNEL_VIEW) {
                    setSeqStep(SeqStep::CHANNEL_VIEW_ACTIVE);
                    sendCommand("channel_view");
                    setState(C5State::MONITORING);
                }
            }
            break;

        case SeqStep::SCAN_REQUESTED:
            // Fallback: some JanOS builds don't print (or vary) the "WiFi scan completed" marker.
            // After a short delay, proactively request results; JanOS will reply with either CSV or
            // "Scan still in progress...".
            if (elapsed >= 8000) {
                setSeqStep(SeqStep::SCAN_SHOW_RESULTS);
                sendCommand("show_scan_results");
            }
            break;

        case SeqStep::TARGET_SELECTING: {
            // Find our target BSSID in the fresh scan cache
            int idx = findBssidInCache(targetBssid);
            if (idx >= 0) {
                targetC5Index = scanCacheActive[idx].c5Index;
                char cmd[48];
                if (currentOp == C5Op::HANDSHAKE) {
                    snprintf(cmd, sizeof(cmd), "start_handshake %d", targetC5Index);
                    sendCommand(cmd);
                    setSeqStep(SeqStep::ATTACK_SENT);
                } else {
                    // SAE: select first, wait for confirm, then overflow
                    snprintf(cmd, sizeof(cmd), "select_networks %d", targetC5Index);
                    sendCommand(cmd);
                    setSeqStep(SeqStep::SAE_SELECT_SENT);
                }
                setState(C5State::ATTACKING);
                C5_LOGF("Target found at C5 index %d", targetC5Index);
            } else {
                // Target not found in C5 scan
                C5_LOGF("Target BSSID not found in C5 scan");
                hsResult = HandshakeResult::FAILED;
                currentOp = C5Op::NONE;
                setSeqStep(SeqStep::IDLE);
                setState(C5State::CONNECTED);
            }
            break;
        }

        case SeqStep::SAE_SELECT_SENT:
            // Wait for "Selected network N:" confirmation (or timeout after 2s)
            if (elapsed >= 2000) {
                // Timeout — send sae_overflow anyway
                sendCommand("sae_overflow");
                setSeqStep(SeqStep::ATTACK_MONITORING);
            }
            break;

        case SeqStep::ATTACK_SENT:
            // Transition to monitoring
            setSeqStep(SeqStep::ATTACK_MONITORING);
            break;

        default:
            break;
    }
}

// ============================================================================
// Internal: Inject Scan Results Into NetworkRecon
// ============================================================================

static void injectScanIntoRecon() {
    uint8_t injected = 0;
    for (uint8_t i = 0; i < scanCountActive; i++) {
        const C5ScanEntry& entry = scanCacheActive[i];
        if (entry.channel <= 14) continue;  // Porkchop already owns 2.4GHz
        NetworkRecon::injectExternal(
            entry.bssid, entry.ssid, entry.rssi,
            entry.channel, (wifi_auth_mode_t)entry.authmode,
            NET_SOURCE_C5
        );
        injected++;
    }
    C5_LOGF("Injected %d 5GHz networks into recon", injected);
}

// ============================================================================
// Internal: BSSID Lookup in Scan Cache
// ============================================================================

static int findBssidInCache(const uint8_t* bssid) {
    for (uint8_t i = 0; i < scanCountActive; i++) {
        if (memcmp(scanCacheActive[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    return -1;
}
