// Menu system - Sirloin-style grouped modal
#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <functional>

// Menu item for both root and group items
struct MenuItem {
    const char* icon;      // 2-char ASCII glyph, "" if none
    const char* label;
    uint8_t actionId;      // 0 = separator, >0 = action
    const char* const* hintPool;
    uint8_t hintCount;
};

// Group identifiers
enum class GroupId : int8_t {
    NONE = -1,
    ATTACK = 0,
    RECON = 1,
    LOOT = 2,
    COMMS = 3,
    RANK = 4,
    SYSTEM = 5
};

// Root menu item types
enum class RootType : uint8_t {
    DIRECT,     // Opens a mode directly
    GROUP,      // Opens a modal group
    SEPARATOR   // Visual separator, not selectable
};

struct RootItem {
    const char* icon;      // 2-char ASCII glyph, "" if none
    const char* label;
    const char* const* hintPool;
    uint8_t hintCount;
    RootType type;
    union {
        uint8_t actionId;   // For DIRECT type
        GroupId groupId;    // For GROUP type
    };
    const char* hint;
};

using MenuCallback = std::function<void(uint8_t actionId)>;

class Menu {
public:
    static void init();
    static void update();
    static void draw(M5Canvas& canvas);
    
    static void setCallback(MenuCallback cb);
    
    static bool isActive() { return active; }
    static bool isInModal() { return activeGroup != GroupId::NONE; }
    static bool closeModal();  // Returns true if modal was closed
    static void show();
    static void hide();
    
    static const char* getSelectedDescription();  // For bottom bar
    
private:
    // Root menu
    static const RootItem ROOT_ITEMS[];
    static const uint8_t ROOT_COUNT;
    static uint8_t rootIdx;
    static uint8_t rootScroll;
    
    // Group modal
    static const MenuItem GROUP_ATTACK[];
    static const MenuItem GROUP_RECON[];
    static const MenuItem GROUP_LOOT[];
    static const MenuItem GROUP_COMMS[];
    static const MenuItem GROUP_RANK[];
    static const MenuItem GROUP_SYSTEM[];
    static const uint8_t GROUP_ATTACK_SIZE;
    static const uint8_t GROUP_RECON_SIZE;
    static const uint8_t GROUP_LOOT_SIZE;
    static const uint8_t GROUP_COMMS_SIZE;
    static const uint8_t GROUP_RANK_SIZE;
    static const uint8_t GROUP_SYSTEM_SIZE;
    
    static GroupId activeGroup;
    static uint8_t modalIdx;
    static uint8_t modalScroll;
    
    // State
    static bool active;
    static MenuCallback callback;
    static bool keyWasPressed;
    
    static const uint8_t VISIBLE_ITEMS = 8;   // Core2: 200px main area fits 8
    static const uint8_t MODAL_VISIBLE = 7;  // Core2: bigger modal
    static uint8_t rootHintIndex[];
    static uint8_t attackHintIndex[];
    static uint8_t reconHintIndex[];
    static uint8_t lootHintIndex[];
    static uint8_t commsHintIndex[];
    static uint8_t rankHintIndex[];
    static uint8_t systemHintIndex[];
    
    // Helpers
    static void handleInput();
    static void drawRoot(M5Canvas& canvas);
    static void drawModal(M5Canvas& canvas);
    static bool isRootSelectable(uint8_t idx);
    static const MenuItem* getGroupItems(GroupId group);
    static uint8_t getGroupSize(GroupId group);
    static const char* getGroupName(GroupId group);
};
