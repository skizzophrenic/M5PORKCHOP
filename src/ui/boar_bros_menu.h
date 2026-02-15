// BOAR BROS Menu - Manage excluded networks
#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <vector>

struct BroInfo {
    uint64_t bssid;      // BSSID as uint64
    char bssidStr[18];   // Formatted BSSID (AA:BB:CC:DD:EE:FF)
    char ssid[33];       // SSID if known (from file comment)
};

class BoarBrosMenu {
public:
    static void init();
    static void show();
    static void hide();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isActive() { return active; }
    static size_t getCount();
    static void getSelectedInfo(char* out, size_t len);
    
private:
    static std::vector<BroInfo> bros;
    static uint8_t selectedIndex;
    static uint8_t scrollOffset;
    static bool active;
    static bool keyWasPressed;
    static bool deleteConfirmActive;
    
    static const uint8_t VISIBLE_ITEMS = 9;
    
    static void handleInput();
    static void loadBros();
    static void deleteSelected();
    static void drawDeleteConfirm(M5Canvas& canvas);
    static void formatBSSID(uint64_t bssid, char* out, size_t len);
};
