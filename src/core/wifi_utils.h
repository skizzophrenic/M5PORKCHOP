#pragma once

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace WiFiUtils {
    enum class TimeSyncStatus : uint8_t {
        OK = 0,
        SKIP_ALREADY_SYNCED,
        SKIP_NOT_CONNECTED,
        SKIP_LOW_RSSI,
        SKIP_LOW_HEAP,
        FAIL_TIMEOUT
    };
    /**
     * @brief Performs a hard reset of the WiFi subsystem
     * @note Does not power off WiFi driver to prevent RX buffer allocation failures
     */
    void hardReset();

    /**
     * @brief Performs a soft shutdown of the WiFi subsystem
     * @note Does not power off WiFi driver to prevent RX buffer allocation failures
     */
    void shutdown();

    /**
     * @brief Stops promiscuous mode if currently active
     */
    void stopPromiscuous();

    /**
     * @brief Ensures system time is synchronized via NTP
     * @param timeoutMs Maximum time to wait for sync (default 6000ms)
     * @param force Force resync even if time appears valid (default false)
     * @return true if time is valid, false if sync failed within timeout
     */
    bool ensureTimeSynced(uint32_t timeoutMs = 6000, bool force = false);

    /**
     * @brief Attempt NTP sync when conditions are good (once per boot on success).
     * @note Non-fatal: returns status describing skip/failure.
     */
    TimeSyncStatus maybeSyncTimeForFileTransfer();
}
