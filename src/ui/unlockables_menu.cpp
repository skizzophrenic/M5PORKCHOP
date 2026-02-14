// Unlockables Menu - Secret challenges for the worthy

#include "unlockables_menu.h"
#include <mbedtls/sha256.h>
#include "display.h"
#include "input.h"
#include "soft_keyboard.h"
#include "../core/xp.h"
#include "../piglet/mood.h"
#include <string.h>

// Static member initialization
uint8_t UnlockablesMenu::selectedIndex = 0;
uint8_t UnlockablesMenu::scrollOffset = 0;
bool UnlockablesMenu::active = false;
bool UnlockablesMenu::keyWasPressed = false;
bool UnlockablesMenu::exitRequested = false;
bool UnlockablesMenu::textEditing = false;
char UnlockablesMenu::textBuffer[33] = "";
uint8_t UnlockablesMenu::textLen = 0;

// The unlockables - secrets for those who seek
// Hash: SHA256(phrase) - lowercase hex, lowercase input
static const UnlockableItem UNLOCKABLES[] = {
    // Bit 0: commit messages speak in riddles
    {
        "PROPHECY",
        "THE PROPHECY SPEAKS THE KEY",
        "13ca9c448763034b2d1b1ec33b449ae90433634c16b50a0a9fba6f4b5a67a72a",
        0
    },
    // Bit 1: persistence is immortality
    {
        "1MM0RT4L",
        "PIG SURVIVES M5BURNER",
        "6c58bc00fea09c8d7fdb97c7b58741ad37bd7ba8e5c76d35076e3b57071b172b",
        1
    },
    // Bit 2: classic unix identity crisis
    {
        "C4LLS1GN",
        "UNIX KNOWS. DO YOU?",
        "73d7b7288d31175792d8a1f51b63936d5683718082f5a401b4e9d6829de967d3",
        2
    },
    // Bit 3: jah bless di herb
    {
        "B4K3D_P1G",
        "JAH PROVIDES. PIG RESTS.",
        "af062b87461d9caa433210fc29a6c1c2aaf28c09c6c54578f16160d7d6a8caa0",
        3
    },
};

static const uint8_t TOTAL_UNLOCKABLES = sizeof(UNLOCKABLES) / sizeof(UNLOCKABLES[0]);

void UnlockablesMenu::init() {
    selectedIndex = 0;
    scrollOffset = 0;
    textEditing = false;
    textBuffer[0] = '\0'; textLen = 0;
}

void UnlockablesMenu::show() {
    active = true;
    exitRequested = false;
    selectedIndex = 0;
    scrollOffset = 0;
    textEditing = false;
    textBuffer[0] = '\0'; textLen = 0;
    keyWasPressed = true;  // Ignore the Enter that selected us from menu
    updateBottomOverlay();
}

void UnlockablesMenu::hide() {
    active = false;
    textEditing = false;
    textBuffer[0] = '\0'; textLen = 0;
    SoftKeyboard::stop();
    Display::clearBottomOverlay();
}

void UnlockablesMenu::update() {
    if (!active) return;
    handleInput();
}

bool UnlockablesMenu::validatePhrase(const char* phrase, const char* expectedHash) {
    // Compute SHA256 of input phrase
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA256, 1 = SHA224
    mbedtls_sha256_update(&ctx, (const uint8_t*)phrase, strlen(phrase));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    
    // Convert hash to hex string
    char hexHash[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hexHash[i * 2], "%02x", hash[i]);
    }
    hexHash[64] = '\0';
    
    // Compare with expected
    return strcmp(hexHash, expectedHash) == 0;
}

void UnlockablesMenu::handleInput() {
    // Touch keyboard flow
    if (textEditing) {
        SoftKeyboard::update();
        bool accepted = false;
        if (SoftKeyboard::consumeDone(accepted)) {
            if (accepted) {
                if (TOTAL_UNLOCKABLES > 0 && selectedIndex < TOTAL_UNLOCKABLES) {
                    // Convert to lowercase for comparison
                    char phrase[33];
                    size_t len = strnlen(textBuffer, 32);
                    for (size_t i = 0; i < len && i + 1 < sizeof(phrase); i++) {
                        phrase[i] = (char)tolower((unsigned char)textBuffer[i]);
                    }
                    phrase[len] = '\0';

                    if (validatePhrase(phrase, UNLOCKABLES[selectedIndex].hashHex)) {
                        XP::setUnlockable(UNLOCKABLES[selectedIndex].bitIndex);
                        Display::showToast("UNLOCKED");
                        Display::flashSiren(3);
                        Mood::adjustHappiness(30);
                    } else {
                        Display::showToast("WRONG");
                        Mood::adjustHappiness(-20);
                    }
                }
            }

            textEditing = false;
            textBuffer[0] = '\0';
            textLen = 0;
            SoftKeyboard::stop();
            updateBottomOverlay();
        }
        return;
    }

    if (Input::up()) {
        if (selectedIndex > 0 && TOTAL_UNLOCKABLES > 0) {
            selectedIndex--;
            if (selectedIndex < scrollOffset) {
                scrollOffset = selectedIndex;
            }
            updateBottomOverlay();
        }
        return;
    }

    if (Input::down()) {
        if (TOTAL_UNLOCKABLES > 0 && selectedIndex < TOTAL_UNLOCKABLES - 1) {
            selectedIndex++;
            if (selectedIndex >= scrollOffset + VISIBLE_ITEMS) {
                scrollOffset = selectedIndex - VISIBLE_ITEMS + 1;
            }
            updateBottomOverlay();
        }
        return;
    }

    if (Input::select() && TOTAL_UNLOCKABLES > 0 && selectedIndex < TOTAL_UNLOCKABLES) {
        if (XP::hasUnlockable(UNLOCKABLES[selectedIndex].bitIndex)) {
            Display::showToast("ALREADY YOURS");
        } else {
            textEditing = true;
            textBuffer[0] = '\0';
            textLen = 0;
            SoftKeyboard::start("PHRASE", textBuffer, sizeof(textBuffer), 32, false);
        }
        return;
    }
}

void UnlockablesMenu::handleTextInput() {
    return;
}

void UnlockablesMenu::draw(M5Canvas& canvas) {
    if (!active) return;
    
    // If text editing, show input overlay
    if (textEditing) {
        SoftKeyboard::draw(canvas);
        return;
    }
    
    canvas.fillSprite(COLOR_BG);
    canvas.setTextColor(COLOR_FG);
    canvas.setTextSize(1);
    
    // Get unlocked status
    uint32_t unlocked = XP::getUnlockables();
    
    // Draw unlockables list
    int y = 2;
    int lineHeight = 18;
    
    for (uint8_t i = scrollOffset; i < TOTAL_UNLOCKABLES && i < scrollOffset + VISIBLE_ITEMS; i++) {
        bool hasIt = (unlocked & (1UL << UNLOCKABLES[i].bitIndex)) != 0;
        
        // Highlight selected
        if (i == selectedIndex) {
            canvas.fillRect(0, y - 1, canvas.width(), lineHeight, COLOR_FG);
            canvas.setTextColor(COLOR_BG);
        } else {
            canvas.setTextColor(COLOR_FG);
        }
        
        // Lock/unlock indicator
        canvas.setCursor(4, y);
        canvas.print(hasIt ? "[X]" : "[ ]");
        
        // Name
        canvas.setCursor(28, y);
        canvas.print(UNLOCKABLES[i].name);
        
        y += lineHeight;
    }
    
    // Scroll indicators
    if (scrollOffset > 0) {
        canvas.setCursor(canvas.width() - 10, 16);
        canvas.setTextColor(COLOR_FG);
        canvas.print("^");
    }
    if (scrollOffset + VISIBLE_ITEMS < TOTAL_UNLOCKABLES) {
        canvas.setCursor(canvas.width() - 10, 16 + (VISIBLE_ITEMS - 1) * lineHeight);
        canvas.setTextColor(COLOR_FG);
        canvas.print("v");
    }
}

void UnlockablesMenu::drawTextInput(M5Canvas& canvas) {
    canvas.fillSprite(COLOR_BG);
    
    // Toast-style input box
    int boxW = 200;
    int boxH = 50;
    int boxX = (canvas.width() - boxW) / 2;
    int boxY = (canvas.height() - boxH) / 2;
    
    // Black border then pink fill
    canvas.fillRoundRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, COLOR_BG);
    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 8, COLOR_FG);
    
    // Black text on pink
    canvas.setTextColor(COLOR_BG, COLOR_FG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_center);
    
    // Title
    canvas.drawString("ENTER THE KEY", canvas.width() / 2, boxY + 6);
    
    // Input field (show what they're typing)
    char displayText[24];
    const char* textSrc = textBuffer;
    if (textLen > 20) {
        const char* tail = textSrc + (textLen - 17);
        snprintf(displayText, sizeof(displayText), "...%s", tail);
    } else {
        strncpy(displayText, textSrc, sizeof(displayText) - 2);
        displayText[sizeof(displayText) - 2] = '\0';
    }
    size_t dlen = strlen(displayText);
    if (dlen + 1 < sizeof(displayText)) {
        displayText[dlen] = '_';
        displayText[dlen + 1] = '\0';
    } else {
        displayText[sizeof(displayText) - 2] = '_';
        displayText[sizeof(displayText) - 1] = '\0';
    }
    canvas.drawString(displayText, canvas.width() / 2, boxY + 26);
    
    canvas.setTextDatum(top_left);
}

void UnlockablesMenu::updateBottomOverlay() {
    if (TOTAL_UNLOCKABLES > 0 && selectedIndex < TOTAL_UNLOCKABLES) {
        Display::setBottomOverlay(UNLOCKABLES[selectedIndex].hint);
    } else {
        Display::setBottomOverlay("NO SECRETS YET");
    }
}
