#include "wifi_utils.h"
#include <M5Unified.h>
#include <Arduino.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <WiFi.h>
#include <freertos/semphr.h>
#include <time.h>
#include "heap_policy.h"
#include "heap_gates.h"

namespace WiFiUtils {

static SemaphoreHandle_t timeSyncMutex = nullptr;
static uint32_t lastTimeSyncMs = 0;
static bool timeSyncedThisBoot = false;

static bool initialized = false;
static portMUX_TYPE initMux = portMUX_INITIALIZER_UNLOCKED;

static void ensureInitialized() {
    if (initialized) return;
    portENTER_CRITICAL(&initMux);
    if (!initialized) {
        if (!timeSyncMutex) timeSyncMutex = xSemaphoreCreateMutex();
        initialized = true;
    }
    portEXIT_CRITICAL(&initMux);
}

static bool isTimeValid() {
    time_t now = time(nullptr);
    return now >= 1700000000;  // ~2023-11-14
}

static void ensureNvsReady() {
    // Safe even if already initialised
    (void)nvs_flash_init();
}

void stopPromiscuous() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}


bool ensureTimeSynced(uint32_t timeoutMs, bool force) {
    if (!force && isTimeValid()) {
        return true;
    }

    if (!timeSyncMutex) {
        timeSyncMutex = xSemaphoreCreateMutex();
    }
    if (!timeSyncMutex) return false;

    if (xSemaphoreTake(timeSyncMutex, 0) != pdTRUE) {
        return isTimeValid();
    }

    // Avoid thrashing NTP if we just synced
    if (!force && (millis() - lastTimeSyncMs < 5UL * 60UL * 1000UL) && isTimeValid()) {
        xSemaphoreGive(timeSyncMutex);
        return true;
    }

    // Start SNTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (isTimeValid()) {
            lastTimeSyncMs = millis();
            // Persist NTP time to battery-backed RTC (BM8563)
            if (M5.Rtc.isEnabled()) {
                time_t ntpNow = time(nullptr);
                struct tm ti;
                gmtime_r(&ntpNow, &ti);
                auto rtcDt = M5.Rtc.getDateTime();
                rtcDt.date.year = ti.tm_year + 1900;
                rtcDt.date.month = ti.tm_mon + 1;
                rtcDt.date.date = ti.tm_mday;
                rtcDt.time.hours = ti.tm_hour;
                rtcDt.time.minutes = ti.tm_min;
                rtcDt.time.seconds = ti.tm_sec;
                M5.Rtc.setDateTime(rtcDt);
            }
            xSemaphoreGive(timeSyncMutex);
            return true;
        }
        delay(100);
        yield();
    }

    xSemaphoreGive(timeSyncMutex);
    return isTimeValid();
}

TimeSyncStatus maybeSyncTimeForFileTransfer() {
    // Respect one successful sync per boot.
    if (timeSyncedThisBoot && isTimeValid()) {
        return TimeSyncStatus::SKIP_ALREADY_SYNCED;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return TimeSyncStatus::SKIP_NOT_CONNECTED;
    }

    int rssi = WiFi.RSSI();
    if (rssi < HeapPolicy::kNtpRssiMinDbm) {
        return TimeSyncStatus::SKIP_LOW_RSSI;
    }

    HeapGates::GateStatus gate = HeapGates::checkGate(
        HeapPolicy::kNtpMinFreeHeap,
        HeapPolicy::kNtpMinContig);
    if (gate.failure != HeapGates::TlsGateFailure::None) {
        return TimeSyncStatus::SKIP_LOW_HEAP;
    }

    uint32_t now = millis();
    if (lastTimeSyncMs != 0 &&
        (now - lastTimeSyncMs) < HeapPolicy::kNtpRetryCooldownMs) {
        return TimeSyncStatus::SKIP_ALREADY_SYNCED;
    }

    bool ok = ensureTimeSynced(HeapPolicy::kNtpTimeoutMs, false);
    lastTimeSyncMs = now;
    if (ok) {
        timeSyncedThisBoot = true;
        return TimeSyncStatus::OK;
    }
    return TimeSyncStatus::FAIL_TIMEOUT;
}

void hardReset() {
    stopPromiscuous();

    // IMPORTANT:
    // Do NOT power WiFi off. That triggers esp_wifi_deinit()/esp_wifi_init()
    // and on no-PSRAM builds it often fails to allocate RX buffers:
    //  wifiLowLevelInit(): esp_wifi_init 257
    WiFi.persistent(false);
    WiFi.setSleep(false);

    // Keep STA mode enabled (driver stays alive)
    WiFi.mode(WIFI_STA);

    // THIS IS THE FIX:
    // disconnect(wifioff=false, eraseap=true)
    // wifioff=true causes driver teardown ? RX buffer allocation failure later
    WiFi.disconnect(false, true);

    delay(HeapPolicy::kWiFiShutdownDelayMs);
    ensureNvsReady();
}

void shutdown() {
    stopPromiscuous();

    // Soft shutdown (no driver teardown)
    WiFi.persistent(false);
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_STA);

    delay(HeapPolicy::kWiFiShutdownDelayMs);
}

}  // namespace WiFiUtils
