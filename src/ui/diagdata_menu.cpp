// DiagData Menu - System diagnostics & GPS raw data (paged)
// Core2 layout: inverted section headers, two-column grid, swipe/BtnC pages

#include "diagdata_menu.h"
#include <SD.h>
#include <time.h>
#include <string.h>
#include "display.h"
#include "input.h"
#include "../core/config.h"
#include "../web/wpasec.h"
#include "../web/wigle.h"
#include "../core/sd_layout.h"
#include "../core/heap_health.h"
#include "../core/heap_policy.h"
#include "../core/wifi_utils.h"
#include "../gps/gps.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

// Static member initialization
bool DiagDataMenu::active = false;
bool DiagDataMenu::keyWasPressed = false;
bool DiagDataMenu::wifiResetConfirmActive = false;
uint16_t DiagDataMenu::cachedWpaCracked = 0;
uint16_t DiagDataMenu::cachedWigleUploaded = 0;
uint32_t DiagDataMenu::lastStatRefreshMs = 0;
uint32_t DiagDataMenu::statRefreshIntervalMs = 2000;
uint8_t DiagDataMenu::currentPage = 0;

void DiagDataMenu::show() {
    active = true;
    keyWasPressed = true;
    wifiResetConfirmActive = false;
    lastStatRefreshMs = 0;
    currentPage = 0;
    HeapHealth::setKnuthEnabled(true);
}

void DiagDataMenu::hide() {
    active = false;
    wifiResetConfirmActive = false;
    HeapHealth::setKnuthEnabled(false);
    WPASec::freeCacheMemory();
    WiGLE::freeUploadedListMemory();
}

void DiagDataMenu::update() {
    if (!active) return;

    if (millis() - lastStatRefreshMs > statRefreshIntervalMs) {
        refreshStats();
    }

    // Hold BtnA = GC
    static uint32_t aPressStartMs = 0;
    static bool gcFired = false;
    constexpr uint32_t kGcHoldMs = 900;
    if (M5.BtnA.isPressed()) {
        if (aPressStartMs == 0) aPressStartMs = millis();
        if (!gcFired && (millis() - aPressStartMs) >= kGcHoldMs) {
            collectGarbage();
            Display::setTopBarMessage("CACHE CLEARED", 3000);
            gcFired = true;
        }
    } else {
        aPressStartMs = 0;
        gcFired = false;
    }

    // WiFi reset confirm modal
    if (wifiResetConfirmActive) {
        if (Input::select()) {
            resetWiFi();
            Display::setTopBarMessage("WIFI RESET", 3000);
            wifiResetConfirmActive = false;
        } else if (Input::up()) {
            wifiResetConfirmActive = false;
        }
        return;
    }

    // BtnA = Save snapshot
    if (Input::up()) {
        saveSnapshot();
        Display::setTopBarMessage("SNAPSHOT SAVED", 3000);
        return;
    }

    // BtnB = context action (WiFi reset on SYS page)
    if (Input::select()) {
        if (currentPage == 0) {
            wifiResetConfirmActive = true;
        }
        return;
    }

    // BtnC = next page
    if (Input::down()) {
        currentPage = (currentPage + 1) % PAGE_COUNT;
        return;
    }

    // Swipe left/right = page switch
    if (Input::swipeLeft()) {
        currentPage = (currentPage + 1) % PAGE_COUNT;
        return;
    }
    if (Input::swipeRight()) {
        currentPage = (currentPage > 0) ? currentPage - 1 : PAGE_COUNT - 1;
        return;
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void DiagDataMenu::draw(M5Canvas& canvas) {
    if (!active) return;

    canvas.fillSprite(COLOR_BG);
    canvas.setTextSize(1);

    switch (currentPage) {
        case 0: drawSystemPage(canvas); break;
        case 1: drawGPSPage(canvas); break;
    }

    if (wifiResetConfirmActive) {
        drawWiFiResetConfirm(canvas);
    }
}

// ---------------------------------------------------------------------------
// Page 1/2 — System: memory, network, power, storage
// ---------------------------------------------------------------------------

void DiagDataMenu::drawSystemPage(M5Canvas& canvas) {
    const uint16_t fg = COLOR_FG;
    const uint16_t bg = COLOR_BG;
    const int ROW_H = 9;
    const int col1 = 4, col2 = 158, col3 = 164, col4 = DISPLAY_W - 4;
    int y = 0;
    char buf[32];

    auto drawSection = [&](const char* title) {
        canvas.fillRect(0, y, DISPLAY_W, 10, fg);
        canvas.setTextColor(bg);
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString(title, 4, y + 1);
        canvas.setTextColor(fg);
        y += 11;
    };

    auto drawRow = [&](const char* label, const char* value, int lx, int rx) {
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString(label, lx, y);
        canvas.setTextDatum(TR_DATUM);
        canvas.drawString(value, rx, y);
    };

    // ---- M3M0RY ----
    canvas.fillRect(0, y, DISPLAY_W, 10, fg);
    canvas.setTextColor(bg);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("M3M0RY", 4, y + 1);
    snprintf(buf, sizeof(buf), "%d/%d", currentPage + 1, PAGE_COUNT);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(buf, DISPLAY_W - 4, y + 1);
    canvas.setTextColor(fg);
    y += 11;

    // HEAP | PSRAM
    snprintf(buf, sizeof(buf), "%u", (unsigned)ESP.getFreeHeap());
    drawRow("HEAP:", buf, col1, col2);
    snprintf(buf, sizeof(buf), "%u", (unsigned)ESP.getFreePsram());
    drawRow("PSRAM:", buf, col3, col4);
    y += ROW_H;

    // MIN FREE | PRESSURE
    snprintf(buf, sizeof(buf), "%u", (unsigned)ESP.getMinFreeHeap());
    drawRow("MIN:", buf, col1, col2);
    {
        static const char* const pressureLabels[] = {"NORMAL", "CAUTION", "WARNING", "CRITICAL"};
        uint8_t pl = static_cast<uint8_t>(HeapHealth::getPressureLevel());
        drawRow("PRESSURE:", pl < 4 ? pressureLabels[pl] : "?", col3, col4);
    }
    y += ROW_H;

    // KNUTH | MIN LARGEST
    snprintf(buf, sizeof(buf), "%.2f", HeapHealth::getKnuthRatio());
    drawRow("KNUTH:", buf, col1, col2);
    snprintf(buf, sizeof(buf), "%u", (unsigned)HeapHealth::getMinFree());
    drawRow("MIN FREE:", buf, col3, col4);
    y += ROW_H;

    // Previous session watermarks (conditional row)
    {
        uint32_t pmf = HeapHealth::getPrevMinFree();
        uint32_t pml = HeapHealth::getPrevMinLargest();
        if (pmf > 0 || pml > 0) {
            snprintf(buf, sizeof(buf), "%u", pmf);
            drawRow("PREV MIN:", buf, col1, col2);
            snprintf(buf, sizeof(buf), "%u", pml);
            drawRow("PREV LRG:", buf, col3, col4);
            y += ROW_H;
        }
    }

    // ---- N3TW0RK ----
    drawSection("N3TW0RK");

    bool wifiUp = WiFi.status() == WL_CONNECTED;
    drawRow("WIFI:", wifiUp ? "CONNECTED" : "DOWN", col1, col2);
    if (wifiUp) {
        IPAddress ip = WiFi.localIP();
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        drawRow("IP:", buf, col3, col4);
    }
    y += ROW_H;

    if (wifiUp) {
        char ssidBuf[33] = "-";
        wifi_config_t conf;
        if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0]) {
            strncpy(ssidBuf, reinterpret_cast<const char*>(conf.sta.ssid), sizeof(ssidBuf) - 1);
            ssidBuf[sizeof(ssidBuf) - 1] = '\0';
        }
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString("SSID:", col1, y);
        canvas.drawString(ssidBuf, 40, y);
        y += ROW_H;
    }

    // ---- P0W3R & ST0R4G3 ----
    drawSection("P0W3R & ST0R4G3");

    // BATT | SD
    snprintf(buf, sizeof(buf), "%d%% %.2fV",
             M5.Power.getBatteryLevel(),
             M5.Power.getBatteryVoltage() / 1000.0f);
    drawRow("BATT:", buf, col1, col2);
    if (Config::isSDAvailable()) {
        uint64_t cardSize = SD.cardSize();
        uint64_t cardFree = SD.totalBytes() > SD.usedBytes()
                          ? SD.totalBytes() - SD.usedBytes() : 0;
        uint32_t mb     = (uint32_t)(cardSize / (1024ULL * 1024ULL));
        uint32_t freeMb = (uint32_t)(cardFree / (1024ULL * 1024ULL));
        snprintf(buf, sizeof(buf), "%u/%uMB", freeMb, mb);
        drawRow("SD:", buf, col3, col4);
    } else {
        drawRow("SD:", "MISSING", col3, col4);
    }
    y += ROW_H;

    // WPA-SEC | WIGLE
    snprintf(buf, sizeof(buf), "%u", (unsigned)cachedWpaCracked);
    drawRow("WPA-SEC:", buf, col1, col2);
    snprintf(buf, sizeof(buf), "%u", (unsigned)cachedWigleUploaded);
    drawRow("WIGLE:", buf, col3, col4);
    y += ROW_H;

    // CHARGING | UPTIME
    drawRow("CHARGING:", M5.Power.isCharging() ? "YES" : "NO", col1, col2);
    {
        uint32_t uptimeSec = millis() / 1000;
        uint32_t hrs  = uptimeSec / 3600;
        uint32_t mins = (uptimeSec % 3600) / 60;
        snprintf(buf, sizeof(buf), "%uh%02um", hrs, mins);
        drawRow("UPTIME:", buf, col3, col4);
    }
    y += ROW_H;

    // ---- Controls ----
    y += 3;
    canvas.drawFastHLine(4, y - 1, DISPLAY_W - 8, fg);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("A:SAVE  B:WIFI RST  C:PAGE>", 4, y);
    y += ROW_H;
    canvas.drawString("HOLD A:GC  SWIPE:PAGE", 4, y);
}

// ---------------------------------------------------------------------------
// Page 2/2 — GPS: status, position, raw NMEA stats
// ---------------------------------------------------------------------------

void DiagDataMenu::drawGPSPage(M5Canvas& canvas) {
    const uint16_t fg = COLOR_FG;
    const uint16_t bg = COLOR_BG;
    const int ROW_H = 9;
    const int col1 = 4, col2 = 158, col3 = 164, col4 = DISPLAY_W - 4;
    int y = 0;
    char buf[32];

    auto drawSection = [&](const char* title) {
        canvas.fillRect(0, y, DISPLAY_W, 10, fg);
        canvas.setTextColor(bg);
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString(title, 4, y + 1);
        canvas.setTextColor(fg);
        y += 11;
    };

    auto drawRow = [&](const char* label, const char* value, int lx, int rx) {
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString(label, lx, y);
        canvas.setTextDatum(TR_DATUM);
        canvas.drawString(value, rx, y);
    };

    GPSData gpsData = GPS::getData();
    bool gpsActive = GPS::isActive();

    // ---- GPS ----
    canvas.fillRect(0, y, DISPLAY_W, 10, fg);
    canvas.setTextColor(bg);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("GPS", 4, y + 1);
    snprintf(buf, sizeof(buf), "%d/%d", currentPage + 1, PAGE_COUNT);
    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(buf, DISPLAY_W - 4, y + 1);
    canvas.setTextColor(fg);
    y += 11;

    if (!gpsActive && !Config::gps().enabled) {
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString("GPS DISABLED IN CONFIG", col1, y);
        y += ROW_H * 2;
    } else {
        // FIX | SATS
        drawRow("FIX:", gpsData.fix ? "YES" : (gpsActive ? "NO" : "SLEEP"), col1, col2);
        snprintf(buf, sizeof(buf), "%d", gpsData.satellites);
        drawRow("SATS:", buf, col3, col4);
        y += ROW_H;

        // HDOP | AGE
        snprintf(buf, sizeof(buf), "%.1f", gpsData.hdop / 100.0f);
        drawRow("HDOP:", buf, col1, col2);
        if (gpsData.age > 99999) {
            drawRow("AGE:", ">99s", col3, col4);
        } else {
            snprintf(buf, sizeof(buf), "%lums", (unsigned long)gpsData.age);
            drawRow("AGE:", buf, col3, col4);
        }
        y += ROW_H;

        // SPEED | COURSE
        snprintf(buf, sizeof(buf), "%.1fkm/h", gpsData.speed);
        drawRow("SPEED:", buf, col1, col2);
        snprintf(buf, sizeof(buf), "%.1f", gpsData.course);
        drawRow("COURSE:", buf, col3, col4);
        y += ROW_H;
    }

    // ---- P0S1T10N ----
    drawSection("P0S1T10N");

    if (gpsData.fix) {
        // LAT | LON
        snprintf(buf, sizeof(buf), "%.6f", gpsData.latitude);
        drawRow("LAT:", buf, col1, col2);
        snprintf(buf, sizeof(buf), "%.6f", gpsData.longitude);
        drawRow("LON:", buf, col3, col4);
        y += ROW_H;

        // ALT
        snprintf(buf, sizeof(buf), "%.1fm", gpsData.altitude);
        drawRow("ALT:", buf, col1, col2);
        y += ROW_H;
    } else {
        canvas.setTextDatum(TL_DATUM);
        canvas.drawString("AWAITING FIX", col1, y);
        y += ROW_H;
    }

    // DATE | TIME (raw GPS values)
    if (gpsData.date > 0) {
        uint8_t day    = gpsData.date / 10000;
        uint8_t month  = (gpsData.date / 100) % 100;
        uint8_t year2  = gpsData.date % 100;
        snprintf(buf, sizeof(buf), "%02d/%02d/%02d", day, month, year2);
        drawRow("DATE:", buf, col1, col2);
    } else {
        drawRow("DATE:", "--/--/--", col1, col2);
    }
    if (gpsData.time > 0) {
        uint8_t hour   = gpsData.time / 1000000;
        uint8_t minute = (gpsData.time / 10000) % 100;
        uint8_t second = (gpsData.time / 100) % 100;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
        drawRow("TIME:", buf, col3, col4);
    } else {
        drawRow("TIME:", "--:--:--", col3, col4);
    }
    y += ROW_H;

    // ---- R4W ----
    drawSection("R4W");

    // CHARS | FIXES
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)GPS::getCharsProcessed());
    drawRow("CHARS:", buf, col1, col2);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)GPS::getFixCount());
    drawRow("FIXES:", buf, col3, col4);
    y += ROW_H;

    // SENT/FIX | CK-FAIL
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)GPS::getSentencesWithFix());
    drawRow("SENT/FIX:", buf, col1, col2);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)GPS::getFailedChecksum());
    drawRow("CK-FAIL:", buf, col3, col4);
    y += ROW_H;

    // CK-PASS | ACTIVE
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)GPS::getPassedChecksum());
    drawRow("CK-PASS:", buf, col1, col2);
    drawRow("ACTIVE:", gpsActive ? "YES" : "NO", col3, col4);
    y += ROW_H;

    // ---- Controls ----
    y += 3;
    canvas.drawFastHLine(4, y - 1, DISPLAY_W - 8, fg);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("A:SAVE  C:<PAGE  SWIPE:PAGE", 4, y);
}

// ---------------------------------------------------------------------------
// WiFi Reset Confirm Modal
// ---------------------------------------------------------------------------

void DiagDataMenu::drawWiFiResetConfirm(M5Canvas& canvas) {
    const int boxW = 190;
    const int boxH = 55;
    const int boxX = (canvas.width() - boxW) / 2;
    const int boxY = (canvas.height() - boxH) / 2 - 5;

    canvas.fillRoundRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, COLOR_BG);
    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 8, COLOR_FG);

    canvas.setTextColor(COLOR_BG, COLOR_FG);
    canvas.setTextDatum(top_center);
    canvas.drawString("RESET WIFI STACK?", boxX + boxW / 2, boxY + 12);
    canvas.drawString("B=YES  A=NO", boxX + boxW / 2, boxY + 34);
    canvas.setTextDatum(top_left);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void DiagDataMenu::saveSnapshot() {
    if (!SD.exists("/")) {
        Display::notify(NoticeKind::WARNING, "NO SD CARD");
        return;
    }

    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char filename[64];
    const char* diagDir = SDLayout::diagnosticsDir();
    const bool hasDiagDir = (strcmp(diagDir, "/") != 0);
    if (hasDiagDir && !SD.exists(diagDir)) {
        SD.mkdir(diagDir);
    }
    const char* sep = hasDiagDir ? "/" : "";
    snprintf(filename, sizeof(filename), "%s%sdiag_%04d%02d%02d_%02d%02d%02d.txt",
             diagDir, sep,
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Display::notify(NoticeKind::WARNING, "SAVE FAILED");
        return;
    }

    file.printf("=== PORKCHOP DIAGNOSTICS SNAPSHOT ===\n");
    file.printf("Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n\n",
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    // Memory
    file.printf("MEMORY:\n");
    file.printf("  Free Heap: %u\n", (unsigned)ESP.getFreeHeap());
    file.printf("  Min Free Heap: %u\n", (unsigned)ESP.getMinFreeHeap());
    if (psramFound()) {
        file.printf("  PSRAM Size: %u\n", (unsigned)ESP.getPsramSize());
        file.printf("  PSRAM Free: %u\n", (unsigned)ESP.getFreePsram());
    }
    file.printf("  Pressure: %d\n", (int)HeapHealth::getPressureLevel());
    file.printf("  Knuth: %.2f\n\n", HeapHealth::getKnuthRatio());

    // WiFi
    file.printf("WIFI:\n");
    file.printf("  Mode: %s\n", WiFi.getMode() == 0 ? "NULL" : WiFi.getMode() == 1 ? "STA" : WiFi.getMode() == 2 ? "AP" : "AP_STA");
    file.printf("  Status: %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    if (WiFi.status() == WL_CONNECTED) {
        file.printf("  SSID: %s\n", WiFi.SSID().c_str());
        file.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
        file.printf("  MAC: %s\n", WiFi.macAddress().c_str());
    }
    file.printf("\n");

    // GPS
    file.printf("GPS:\n");
    file.printf("  Active: %s\n", GPS::isActive() ? "YES" : "NO");
    GPSData gd = GPS::getData();
    file.printf("  Fix: %s\n", gd.fix ? "YES" : "NO");
    file.printf("  Satellites: %d\n", gd.satellites);
    file.printf("  HDOP: %.1f\n", gd.hdop / 100.0f);
    file.printf("  Age: %lu ms\n", (unsigned long)gd.age);
    if (gd.fix) {
        file.printf("  Lat: %.6f\n", gd.latitude);
        file.printf("  Lon: %.6f\n", gd.longitude);
        file.printf("  Alt: %.1f m\n", gd.altitude);
        file.printf("  Speed: %.1f km/h\n", gd.speed);
        file.printf("  Course: %.1f\n", gd.course);
    }
    file.printf("  Chars Processed: %lu\n", (unsigned long)GPS::getCharsProcessed());
    file.printf("  Sentences w/Fix: %lu\n", (unsigned long)GPS::getSentencesWithFix());
    file.printf("  Checksum Pass: %lu\n", (unsigned long)GPS::getPassedChecksum());
    file.printf("  Checksum Fail: %lu\n\n", (unsigned long)GPS::getFailedChecksum());

    // System
    file.printf("SYSTEM:\n");
    file.printf("  SDK: %s\n", ESP.getSdkVersion());
    file.printf("  Chip: %s\n", ESP.getChipModel());
    file.printf("  Cores: %d\n", ESP.getChipCores());
    file.printf("  CPU: %d MHz\n", ESP.getCpuFreqMHz());
    file.printf("  Flash: %u MB\n\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));

    // Power
    file.printf("POWER:\n");
    file.printf("  Battery: %d%% (%.2f V)\n", M5.Power.getBatteryLevel(), M5.Power.getBatteryVoltage() / 1000.0f);
    file.printf("  Charging: %s\n", M5.Power.isCharging() ? "YES" : "NO");

    file.close();
}

void DiagDataMenu::resetWiFi() {
    WiFiUtils::hardReset();
}

void DiagDataMenu::collectGarbage() {
    WPASec::freeCacheMemory();
    WiGLE::freeUploadedListMemory();
    yield();
}

void DiagDataMenu::refreshStats() {
    if (!WPASec::isBusy()) {
        cachedWpaCracked = WPASec::getCrackedCount();
    }
    if (!WiGLE::isBusy()) {
        cachedWigleUploaded = WiGLE::getUploadedCount();
    }
    WPASec::freeCacheMemory();
    WiGLE::freeUploadedListMemory();
    lastStatRefreshMs = millis();
}
