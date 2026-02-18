// BOAR BROS Menu - Manage excluded networks

#include "boar_bros_menu.h"
#include <SD.h>
#include <ctype.h>
#include <string.h>
#include "display.h"
#include "input.h"
#include "haptic.h"
#include "../modes/oink.h"
#include "../core/sd_layout.h"

// Static member initialization
std::vector<BroInfo> BoarBrosMenu::bros;
uint8_t BoarBrosMenu::selectedIndex = 0;
uint8_t BoarBrosMenu::scrollOffset = 0;
bool BoarBrosMenu::active = false;
bool BoarBrosMenu::keyWasPressed = false;
bool BoarBrosMenu::deleteConfirmActive = false;

void BoarBrosMenu::init() {
    bros.clear();
    selectedIndex = 0;
    scrollOffset = 0;
}

void BoarBrosMenu::show() {
    active = true;
    selectedIndex = 0;
    scrollOffset = 0;
    keyWasPressed = true;  // Ignore the Enter that selected us from menu
    deleteConfirmActive = false;
    loadBros();
}

void BoarBrosMenu::hide() {
    active = false;
    deleteConfirmActive = false;
    bros.clear();
    bros.shrink_to_fit();  // Release vector memory
}

void BoarBrosMenu::loadBros() {
    bros.clear();
    bros.reserve(50);  // Max entries — avoids 6 reallocations during load

    const char* boarPath = SDLayout::boarBrosPath();
    if (!SD.exists(boarPath)) {
        Serial.println("[BOAR_BROS] No file found");
        return;
    }
    
    File f = SD.open(boarPath, FILE_READ);
    if (!f) {
        Serial.println("[BOAR_BROS] Failed to open file");
        return;
    }
    
    // Cap at 50 entries (same as MAX_BOAR_BROS in oink.cpp)
    // Use stack buffer instead of String to avoid 50 heap alloc/free cycles
    char lineBuf[80];  // BSSID(12) + space + SSID(32) + margin
    while (f.available() && bros.size() < 50) {
        int len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        if (len <= 0) continue;
        lineBuf[len] = '\0';

        // Trim trailing whitespace
        while (len > 0 && (lineBuf[len-1] == '\r' || lineBuf[len-1] == ' ' || lineBuf[len-1] == '\t')) {
            lineBuf[--len] = '\0';
        }

        // Skip leading whitespace
        const char* p = lineBuf;
        while (*p == ' ' || *p == '\t') p++;
        len = strlen(p);

        // Skip empty lines and comments
        if (len == 0 || *p == '#') continue;

        // Format: AABBCCDDEEFF [SSID]
        if (len >= 12) {
            uint64_t bssid = 0;
            bool valid = true;
            for (int i = 0; i < 12; i++) {
                char c = toupper((unsigned char)p[i]);
                uint8_t nibble;
                if (c >= '0' && c <= '9') nibble = c - '0';
                else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                else { valid = false; break; }
                bssid = (bssid << 4) | nibble;
            }

            if (valid) {
                BroInfo info;
                memset(&info, 0, sizeof(info));
                info.bssid = bssid;
                formatBSSID(bssid, info.bssidStr, sizeof(info.bssidStr));

                // Extract SSID from rest of line (after space)
                if (len > 13) {
                    const char* ssid = p + 13;
                    while (*ssid == ' ' || *ssid == '\t') ssid++;
                    if (*ssid) {
                        strncpy(info.ssid, ssid, sizeof(info.ssid) - 1);
                    }
                }

                bros.push_back(info);
            }
        }
    }
    
    f.close();
    Serial.printf("[BOAR_BROS] Loaded %d bros\n", (int)bros.size());
}

void BoarBrosMenu::formatBSSID(uint64_t bssid, char* out, size_t len) {
    if (!out || len == 0) return;
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)((bssid >> 40) & 0xFF),
             (uint8_t)((bssid >> 32) & 0xFF),
             (uint8_t)((bssid >> 24) & 0xFF),
             (uint8_t)((bssid >> 16) & 0xFF),
             (uint8_t)((bssid >> 8) & 0xFF),
             (uint8_t)(bssid & 0xFF));
}

size_t BoarBrosMenu::getCount() {
    return OinkMode::getExcludedCount();
}

void BoarBrosMenu::getSelectedInfo(char* out, size_t len) {
    if (!out || len == 0) return;
    if (bros.empty()) {
        snprintf(out, len, "USE OINK TO ADD BROS");
        return;
    }
    if (selectedIndex < bros.size()) {
        strncpy(out, bros[selectedIndex].bssidStr, len - 1);
        out[len - 1] = '\0';
        return;
    }
    out[0] = '\0';
}

void BoarBrosMenu::update() {
    if (!active) return;
    handleInput();
}

void BoarBrosMenu::handleInput() {
    if (deleteConfirmActive) {
        Input::TapEvent tmp;
        if (Input::select() || Input::tap(tmp)) {
            deleteSelected();
            deleteConfirmActive = false;
        } else if (Input::up()) {
            deleteConfirmActive = false;
        }
        return;
    }

    // Tap-to-select: startY=2, lineHeight=20
    Input::TapEvent tapEv;
    if (Input::tap(tapEv)) {
        if (!bros.empty()) {
            int canvasY = tapEv.y - TOP_BAR_H;
            int hitIdx = (canvasY - 2) / 20;
            if (hitIdx >= 0 && hitIdx < VISIBLE_ITEMS) {
                uint8_t idx = scrollOffset + hitIdx;
                if (idx < bros.size()) {
                    if (idx == selectedIndex) {
                        deleteConfirmActive = true;
                    } else {
                        selectedIndex = idx;
                    }
                }
            }
        }
        return;
    }

    // Vertical swipe for page scrolling
    if (Input::swipeUp()) {
        if (selectedIndex > 0) {
            int n = (int)selectedIndex - VISIBLE_ITEMS;
            selectedIndex = n < 0 ? 0 : n;
            if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
        } else {
            Haptic::stop();
        }
        return;
    }
    if (Input::swipeDown()) {
        if (!bros.empty() && selectedIndex < bros.size() - 1) {
            int n = (int)selectedIndex + VISIBLE_ITEMS;
            if (n >= (int)bros.size()) n = bros.size() - 1;
            selectedIndex = n;
            if (selectedIndex >= scrollOffset + VISIBLE_ITEMS)
                scrollOffset = selectedIndex - VISIBLE_ITEMS + 1;
        } else {
            Haptic::stop();
        }
        return;
    }

    if (Input::up()) {
        if (selectedIndex > 0) {
            selectedIndex--;
            if (selectedIndex < scrollOffset) {
                scrollOffset = selectedIndex;
            }
        } else {
            Haptic::stop();
        }
    }

    if (Input::down()) {
        if (!bros.empty() && selectedIndex < bros.size() - 1) {
            selectedIndex++;
            if (selectedIndex >= scrollOffset + VISIBLE_ITEMS) {
                scrollOffset = selectedIndex - VISIBLE_ITEMS + 1;
            }
        } else {
            Haptic::stop();
        }
    }

    if (Input::select() && !bros.empty()) {
        deleteConfirmActive = true;
    }
}

void BoarBrosMenu::deleteSelected() {
    if (selectedIndex >= bros.size()) return;
    
    uint64_t targetBssid = bros[selectedIndex].bssid;
    
    // Remove from OinkMode's set and save
    OinkMode::removeBoarBro(targetBssid);
    
    // Refresh our list
    loadBros();
    
    // Adjust selection if needed
    if (selectedIndex >= bros.size() && selectedIndex > 0) {
        selectedIndex--;
    }
    if (scrollOffset > 0 && scrollOffset >= bros.size()) {
        scrollOffset = bros.size() > 0 ? bros.size() - 1 : 0;
    }
    
    Display::notify(NoticeKind::STATUS, "BRO REMOVED!");
}

void BoarBrosMenu::draw(M5Canvas& canvas) {
    if (!active) return;
    
    canvas.fillSprite(COLOR_BG);
    canvas.setTextColor(COLOR_FG);
    canvas.setTextSize(1);
    
    if (bros.empty()) {
        canvas.setCursor(4, 35);
        canvas.print("NO BOAR BROS YET!");
        canvas.setCursor(4, 50);
        canvas.print("EXCLUDE IN OINK MODE");
        canvas.setCursor(4, 65);
        canvas.print("TO EXCLUDE A NETWORK.");
        return;
    }
    
    // Draw bros list
    int y = 2;
    int lineHeight = 20;

    for (uint8_t i = scrollOffset; i < bros.size() && i < scrollOffset + VISIBLE_ITEMS; i++) {
        const BroInfo& bro = bros[i];
        
        // Highlight selected
        if (i == selectedIndex) {
            canvas.fillRect(0, y - 1, canvas.width(), lineHeight, COLOR_FG);
            canvas.setTextColor(COLOR_BG);
        } else {
            canvas.setTextColor(COLOR_FG);
        }
        
        // SSID or "NONAME BRO" for hidden networks
        canvas.setCursor(4, y);
        const char* nameSrc = bro.ssid[0] != '\0' ? bro.ssid : "NONAME BRO";
        char displayName[20];
        size_t pos = 0;
        while (*nameSrc && pos + 1 < sizeof(displayName)) {
            displayName[pos++] = (char)toupper((unsigned char)*nameSrc++);
        }
        displayName[pos] = '\0';
        if (pos > 14 && sizeof(displayName) > 14) {
            displayName[12] = '.';
            displayName[13] = '.';
            displayName[14] = '\0';
        }
        canvas.print(displayName);
        
        // Full BSSID (fits at x=80, 17 chars * 6px = 102px, ends at 182px)
        canvas.setCursor(80, y);
        canvas.print(bro.bssidStr);
        
        y += lineHeight;
    }
    
    // Scroll indicators
    if (scrollOffset > 0) {
        canvas.setCursor(canvas.width() - 10, 2);
        canvas.setTextColor(COLOR_FG);
        canvas.print("^");
    }
    if (scrollOffset + VISIBLE_ITEMS < bros.size()) {
        canvas.setCursor(canvas.width() - 10, 2 + (VISIBLE_ITEMS - 1) * lineHeight);
        canvas.setTextColor(COLOR_FG);
        canvas.print("v");
    }
    
    // Draw delete confirmation modal if active
    if (deleteConfirmActive) {
        drawDeleteConfirm(canvas);
    }
}

void BoarBrosMenu::drawDeleteConfirm(M5Canvas& canvas) {
    // Modal box dimensions - matches other confirmation dialogs
    const int boxW = 180;
    const int boxH = 55;
    const int boxX = (canvas.width() - boxW) / 2;
    const int boxY = (canvas.height() - boxH) / 2 - 5;
    
    // Inverted toast: fg border, bg fill, fg text
    canvas.fillRoundRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, COLOR_FG);
    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 8, COLOR_BG);

    canvas.setTextColor(COLOR_FG, COLOR_BG);
    canvas.setTextDatum(top_center);
    
    canvas.drawString("REMOVE THIS BRO?", boxX + boxW / 2, boxY + 10);
    
    const BroInfo& bro = bros[selectedIndex];
    const char* broSrc = bro.ssid[0] != '\0' ? bro.ssid : bro.bssidStr;
    char broName[24];
    size_t broPos = 0;
    while (*broSrc && broPos + 1 < sizeof(broName)) {
        broName[broPos++] = (char)toupper((unsigned char)*broSrc++);
    }
    broName[broPos] = '\0';
    if (broPos > 18 && sizeof(broName) > 18) {
        broName[16] = '.';
        broName[17] = '.';
        broName[18] = '\0';
    }
    canvas.drawString(broName, boxX + boxW / 2, boxY + 24);
    
    canvas.drawString("B=YES  A=NO", boxX + boxW / 2, boxY + 40);
    
    canvas.setTextDatum(top_left);
}
