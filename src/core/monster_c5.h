// MonsterC5 - ESP32-C5 Coprocessor Service (JANUS HOG)
// Peripheral service like GPS — always-on when enabled, feeds 5GHz data
// into NetworkRecon pipeline. Modes don't need to know it exists until
// they route a 5GHz attack.
#pragma once

#include <Arduino.h>
#include <esp_wifi.h>

// Network source tags for DetectedNetwork.source
#define NET_SOURCE_LOCAL  0
#define NET_SOURCE_C5     1

enum class C5State : uint8_t {
    OFF,            // Config disabled or not initialized
    DISCONNECTED,   // Probing for C5 on UART (ping every 2s)
    CONNECTED,      // Idle, ready for commands
    SCANNING,       // scan_networks in progress
    ATTACKING,      // handshake/sae/deauth running on C5
    MONITORING,     // channel_view or packet_monitor running
    ERROR           // Backoff retry (exponential: 5/10/20/30s)
};

enum class C5Op : uint8_t {
    NONE,
    SCAN,
    HANDSHAKE,
    SAE_OVERFLOW,
    CHANNEL_VIEW,
    PACKET_MONITOR,
    SNIFFER_DOG,
    BLACKOUT
};

enum class HandshakeResult : uint8_t {
    IDLE,
    IN_PROGRESS,
    CAPTURED,
    FAILED
};

// Internal scan cache entry (maps to JanOS CSV output)
struct C5ScanEntry {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  authmode;      // wifi_auth_mode_t mapped from JanOS string
    uint8_t  c5Index;       // 1-based index for select_networks command
};

// Channel utilization counts from channel_view
struct C5ChannelCounts {
    uint16_t ch24[14];      // channels 1-14 (2.4GHz)
    uint16_t ch5[25];       // channels 36,40,...,165 (5GHz)
    bool valid;
};

namespace MonsterC5 {

// ============================================================================
// Lifecycle (called from main.cpp, like GPS)
// ============================================================================

void init();            // After Config load, before modes
void update();          // Every main loop iteration. Non-blocking.
void shutdown();        // Send stop, release UART, resume GPS.

// ============================================================================
// State Queries
// ============================================================================

C5State getState();
C5Op    getCurrentOp();
bool    isConnected();  // CONNECTED or any active state
bool    isEnabled();    // Config::c5().enabled

// ============================================================================
// Active Commands (called by modes)
// ============================================================================

bool requestScan();                             // Scan 2.4+5GHz on C5
bool requestHandshake(const uint8_t* bssid);    // Full sequence: stop->scan->select->attack
bool requestSaeOverflow(const uint8_t* bssid);
bool requestChannelView();                      // Continuous channel utilization
bool requestPacketMonitor(uint8_t channel);     // PPS on specific channel
void requestStop();                             // Stop current C5 operation

// ============================================================================
// Result Polling (checked by modes in their update())
// ============================================================================

HandshakeResult getHandshakeResult();
void clearHandshakeResult();

const C5ChannelCounts& getChannelCounts();
uint32_t getPacketsPerSecond();

// ============================================================================
// Scan Cache Access (for command sequencer BSSID lookup)
// ============================================================================

uint8_t getScanCount();
const C5ScanEntry* getScanEntry(uint8_t index);

// ============================================================================
// Status String (for UI overlay)
// ============================================================================

void getStatusString(char* buf, uint8_t len);

} // namespace MonsterC5
