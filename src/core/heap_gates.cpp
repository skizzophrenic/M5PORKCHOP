// HeapGates - Core2 build
// Core2 has 4MB PSRAM + 320KB DRAM. Heap gating is unnecessary.
// All gates pass unconditionally.

#include "heap_gates.h"
#include "heap_policy.h"
#include <Arduino.h>

namespace HeapGates {

// Always-pass: Core2 has abundant memory
static constexpr size_t FAKE_FREE  = 200000;
static constexpr size_t FAKE_BLOCK = 180000;

TlsGateStatus checkTlsGates() {
    return {FAKE_FREE, FAKE_BLOCK, TlsGateFailure::None};
}

bool canTls(const TlsGateStatus& status, char*, size_t) {
    return true;
}

GateStatus checkGate(size_t minFree, size_t minContig) {
    return {FAKE_FREE, FAKE_BLOCK, minFree, minContig, TlsGateFailure::None};
}

bool canMeet(const GateStatus&, char*, size_t) {
    return true;
}

bool shouldProactivelyCondition(const TlsGateStatus&) {
    return false;
}

HeapSnapshot snapshot() {
    return {FAKE_FREE, FAKE_BLOCK, 0.9f};
}

bool canGrow(const HeapSnapshot&, size_t, float) {
    return true;
}

bool canGrow(size_t, float) {
    return true;
}

void waitForLwipCleanup() {
    // No-op on Core2
}

}  // namespace HeapGates
