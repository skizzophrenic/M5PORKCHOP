// DiagData Menu - System diagnostics & GPS raw data (paged)
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class DiagDataMenu {
public:
    static void show();
    static void hide();
    static void update();
    static bool isActive() { return active; }
    static void draw(M5Canvas& canvas);

private:
    static bool active;
    static bool keyWasPressed;
    static bool wifiResetConfirmActive;
    static uint16_t cachedWpaCracked;
    static uint16_t cachedWigleUploaded;
    static uint32_t lastStatRefreshMs;
    static uint32_t statRefreshIntervalMs;
    static uint8_t currentPage;
    static constexpr uint8_t PAGE_COUNT = 2;

    static void saveSnapshot();
    static void resetWiFi();
    static void collectGarbage();
    static void refreshStats();
    static void drawSystemPage(M5Canvas& canvas);
    static void drawGPSPage(M5Canvas& canvas);
    static void drawWiFiResetConfirm(M5Canvas& canvas);
};
