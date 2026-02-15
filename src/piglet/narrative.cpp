#include "narrative.h"
#include <esp_random.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <M5Unified.h>
#include "../core/xp.h"
#include "../core/network_recon.h"
#include "../modes/oink.h"

// ============================================================
// NARRATIVE ENGINE — combinatorial D&D event log generator
// Templates × noun pools × adjectives × d20 rolls = millions
// of unique lines from ~6KB flash. The pig narrates everything.
// ============================================================

// --- Noun pool (30 words, max 8 chars) ---
// Mix of porkchop lore, D&D, Monty Python, Bard's Tale, and tech.
// Comedy comes from cross-context collisions.
static const char* const NOUNS[] = {
    "PIG",      "MUD",      "ORC",      "ALE",
    "BARN",     "BARD",     "HEAP",     "JTAG",
    "LUTE",     "SNAKE",    "SNOUT",    "HORSE",
    "GRAIL",    "WITCH",    "STACK",    "ROGUE",
    "QUEST",    "PARROT",   "MALLOC",   "GOBLIN",
    "DRAGON",   "TAVERN",   "KNIGHT",   "TROUGH",
    "TRUFFLE",  "DUNGEON",  "SWALLOW",  "COCONUT",
    "FIRMWARE", "WATCHDOG"
};
static constexpr uint8_t NOUN_COUNT = 30;

// --- Adjective pool (14 words, max 8 chars) ---
static const char* const ADJECTIVES[] = {
    "FERAL",    "HOLY",     "CURSED",   "HAUNTED",
    "ANCIENT",  "UNDEAD",   "SACRED",   "ARCANE",
    "BLESSED",  "ELDRITCH", "VOLATILE", "HOSTILE",
    "TACTICAL", "SENTIENT"
};
static constexpr uint8_t ADJ_COUNT = 14;

// ============================================================
// TEMPLATE CATEGORIES
// Slots: {S}=subject noun, {O}=object noun,
//        {A}=adjective, {N}=d20 roll (1-20)
// ALL templates verified <=53 chars at worst case
// (8-char noun + 8-char adj + 2-char roll)
// ============================================================

// --- IDLE: Bard's Tale tavern + Monty Python absurdism ---
static const char* const TPL_IDLE[] = {
    "THE {S} SITS IN THE {O}. NOTHING HAPPENS.",
    "{A} {S} WANTS TO DISCUSS {O}.",
    "NOBODY EXPECTS THE {A} {S}.",
    "WE ARE THE KNIGHTS WHO SAY {S}.",
    "BARD SINGS OF {A} {S}. EVERYONE LEAVES.",
    "YOUR {S} HAS BEEN CURSED. IT IS NOW A {O}.",
    "'{S}?' SAYS THE {O}. 'NEVER HEARD OF IT.'",
    "THE {S} WOULD LIKE TO RAGE.",
    "A WILD {S} APPEARS. IT IS {A}.",
    "THE {S} IS MERELY RESTING. PINING FOR {O}.",
    "THE {S} OFFERS TO JOIN THE PARTY. DENIED.",
    "WHAT IS THE AIRSPEED OF AN UNLADEN {S}?",
    "YOU FIND A {S} IN THE {O}. IT BITES.",
    "THE {O} DEMANDS SACRIFICE. {S} VOLUNTEERS.",
    "THE {S} ORDERS {A} {O} AT THE BAR.",
    "INNKEEPER WARNS OF {A} {S} IN THE {O}.",
    "YOU OPEN THE CHEST. INSIDE: {A} {S}.",
};

// --- HUNTING: D&D encounter log ---
static const char* const TPL_HUNT[] = {
    "{S} ROLLS {N} PERCEPTION. {O} HIDES.",
    "{S} CASTS DETECT {O}. THE ETHER SAYS NO.",
    "INITIATIVE: {S} {N}. SURPRISE ROUND.",
    "ENCOUNTER: {A} {S}. ROLL FOR INIT.",
    "A WANDERING {S} APPEARS. NO LOOT YET.",
    "{S} ENTERS THE {O}. SAVING THROW: {N}.",
    "THE {A} {O} IS NEAR. {S} CAN FEEL IT.",
    "{S} SEARCHES THE {O}. FINDS ONLY DUST.",
    "{S} SNIFFS. SOMETHING {A} THIS WAY COMES.",
    "THE {O} LURKS IN SHADOW. {S} ROLLS {N}.",
    "{S} CHECKS FOR TRAPS. ROLL: {N}. NOTHING.",
    "THE {A} {O} ELUDES THE {S}. FOR NOW.",
    "{S} READIES WEAPON. THE {O} WATCHES.",
};

// --- VICTORY: Baldur's Gate triumph ---
static const char* const TPL_WIN[] = {
    "{O} HAS FALLEN. {S} CLAIMS {A} LOOT.",
    "{S} CRITS FOR {N}. THE {O} IS NO MORE.",
    "QUEST DONE: THE {A} {O} HAS BEEN PWNED.",
    "{S} VANQUISHES {O}. BARD TAKES NOTES.",
    "VICTORY. {S} GAINS {N} XP. PIG APPROVES.",
    "{S} LOOTS {O}. FOUND: {A} TRUFFLE.",
    "NATURAL {N}! {S} DESTROYS THE {O}.",
    "THE {O} DROPS A HANDSHAKE. {S} TAKES IT.",
    "{S} SLAYS {A} {O}. PRAISE THE SUN.",
    "{S} ROLLED NAT {N}. {O} IS VANQUISHED.",
};

// --- DEFEAT: Monty Python Black Knight + existential ---
static const char* const TPL_FAIL[] = {
    "TIS BUT A SCRATCH. THE {S} HAS NO {O}.",
    "THE {S} IS NOT DEAD. IT GETS BETTER.",
    "NONE SHALL PASS. ESPECIALLY THE {A} {S}.",
    "{S} FLED. THE {O} DIDNT EVEN NOTICE.",
    "YOUR {S} DIED. {O} SHRUGS.",
    "RUN AWAY! THE {A} {S} HAS TEETH!",
    "BRING OUT YOUR DEAD. THE {S} OBJECTS.",
    "THE {S} HAS CEASED TO BE. PINING FOR {O}.",
    "YOU DIED. {S} WATCHES FROM THE {O}.",
    "TPK. BLAME THE {S}. IT BLAMES THE {O}.",
    "THE {S} TAKES AN ARROW TO THE {O}.",
    "{S} ROLLS {N}. CRITICAL FAIL. OH NO.",
};

// --- LORE: porkchop mythology + Monty Python deep cuts ---
static const char* const TPL_LORE[] = {
    "THE HORSE CONFIRMS: THE BARN IS STRUCTURAL.",
    "HORSE FOUND THE K. {S} LOOKS AWAY.",
    "THE COLON INCIDENT REMAINS CLASSIFIED.",
    "YOUR MUM WAS A {S}. DAD SMELT OF {O}.",
    "THE {S} WEIGHS SAME AS A DUCK. THEREFORE.",
    "BARN APPEALS THE RULING. HORSE IS THE BARN.",
    "THE {S} OF {O}. UNDER THE BARN.",
    "SOUP RECIPE: {A} {S}. STIR TILL COMPILED.",
    "K > OPTOMETRIST. {S} AGREES. BARN DENIES.",
    "NI! WE DEMAND A {S}! A {A} ONE!",
    "{S} IS A WITCH. TURNED ME INTO A {O}.",
    "THE BARN IS LOAD-BEARING. {S} CONFIRMS.",
    "HORSE STATUS: {A}. BARN STATUS: {A}.",
    "THE {S} HAS A CUNNING PLAN INVOLVING {O}.",
    "ON SECOND THOUGHT LETS NOT GO TO {O}.",
};

// --- AWARE: situational templates using real game data ---
// Multi-char slots: {NETS} {HS} {CH} {BAT} {LVL}
static const char* const TPL_AWARE[] = {
    "{NETS} NETWORKS QUAKE BEFORE THE {A} {S}.",
    "CHANNEL {CH}: THE {S} LURKS IN SHADOW.",
    "{HS} HANDSHAKES COLLECTED. {S} HUNGERS.",
    "BATTERY AT {BAT}%. THE {A} {S} GROWS WEARY.",
    "L{LVL} {S} SURVEYS THE {O}. {NETS} TARGETS.",
    "THE {A} {S} TUNES TO CH{CH}. SOMETHING STIRS.",
    "{HS} SOULS HARVESTED. THE {O} IS NEXT.",
    "AT {BAT}%, THE {S} CONSIDERS RETIREMENT.",
    "LVL {LVL}: THE {A} {S} HAS SEEN {NETS} REALMS.",
    "THE {O} HIDES ON CH{CH}. {S} ROLLS {N}.",
};

// --- D20: combat roll result templates ---
// Single roll: {D20}=roll value, {XP}=xp result
static const char* const TPL_D20[] = {
    "{S} ROLLS {D20}. {A} BLOW FOR {XP}XP.",
    "D20:{D20}! THE {A} {S} STRIKES FOR {XP}XP.",
    "{S} ROLLS {D20} VS {O}. {XP}XP CLAIMED.",
    "THE {A} {S} ATTACKS. D20:{D20}. {XP}XP!",
};
static const char* const TPL_D20_CRIT[] = {
    "NATURAL 20! {S} CRITS THE {O}. {XP}XP!",
    "NAT 20! THE {A} {S} OBLITERATES. {XP}XP!",
    "CRIT! {S} DEVASTATES {O}. {XP}XP CLAIMED!",
};
static const char* const TPL_D20_FUMBLE[] = {
    "FUMBLE! {S} TRIPS OVER THE {A} {O}. {XP}XP.",
    "NAT 1! THE {S} HITS ITSELF. {XP}XP. OOF.",
    "CRITICAL FAIL. {S} EMBARRASSES THE {O}.",
};
// Batch templates: {COUNT}=number of rolls
static const char* const TPL_D20_BATCH[] = {
    "{COUNT}x D20: {S} UNLEASHES {A} FURY. {XP}XP.",
    "{COUNT} ROLLS. THE {A} {S} CLAIMS {XP}XP.",
    "{S} ROLLS {COUNT}x. THE {O} YIELDS {XP}XP.",
};
static const char* const TPL_D20_BATCH_CRIT[] = {
    "NAT 20 IN {COUNT} ROLLS! {S} CRITS. {XP}XP!",
    "{COUNT}x D20 W/ CRIT! {A} {S} EARNS {XP}XP!",
};

// ============================================================
// STATE
// ============================================================
static char sLine1[54] = "";
static char sLine2[54] = "";
static char sLine3[54] = "";
static uint32_t sLastUpdate = 0;
static uint32_t sLastFlashMs = 0;
static constexpr uint32_t UPDATE_INTERVAL_MS = 9000;
static uint8_t sLastTplIdx = 255;
static bool sReady = false;

// Event queue (newest wins)
static NarrativeEvent sPendingEvent = EVT_NONE;

// D20 roll accumulator
static uint8_t sPendingRollCount = 0;
static uint8_t sLastRoll = 0;
static uint8_t sBestRoll = 0;       // Track best roll in batch for crit detection
static uint16_t sTotalRollXP = 0;
static uint32_t sFirstRollMs = 0;
static constexpr uint32_t ROLL_BATCH_WAIT_MS = 2000;  // 2s to collect batch

// ============================================================
// SLOT EXPANDER — replaces all slot types in template
// Single-char: {S} {O} {A} {N}
// Multi-char:  {NETS} {HS} {CH} {BAT} {LVL} {D20} {XP} {COUNT}
// ============================================================

// Helper: try to match a multi-char slot starting at tpl
// Returns length consumed (including braces) or 0 if no match
// Writes value to dst via pointer-to-pointer (advances caller's dst)
static int tryExpandMulti(const char* tpl, char** pDst, const char* end) {
    if (*tpl != '{') return 0;

    // Build lookup for each multi-char slot
    struct Slot { const char* tag; int tagLen; char val[8]; };
    // Populate values on-demand
    uint16_t nets = NetworkRecon::getNetworkCount();
    uint16_t hs = XP::getSession().handshakes;
    uint8_t ch = OinkMode::getChannel();
    int bat = M5.Power.getBatteryLevel();
    uint8_t lvl = XP::getLevel();

    Slot slots[] = {
        { "{NETS}",  6, "" },
        { "{HS}",    4, "" },
        { "{CH}",    4, "" },
        { "{BAT}",   5, "" },
        { "{LVL}",   5, "" },
        { "{D20}",   5, "" },
        { "{XP}",    4, "" },
        { "{COUNT}", 7, "" },
    };
    snprintf(slots[0].val, sizeof(slots[0].val), "%u", nets);
    snprintf(slots[1].val, sizeof(slots[1].val), "%u", hs);
    snprintf(slots[2].val, sizeof(slots[2].val), "%u", ch);
    snprintf(slots[3].val, sizeof(slots[3].val), "%d", bat);
    snprintf(slots[4].val, sizeof(slots[4].val), "%u", lvl);
    snprintf(slots[5].val, sizeof(slots[5].val), "%u", sLastRoll);
    snprintf(slots[6].val, sizeof(slots[6].val), "%u", sTotalRollXP);
    snprintf(slots[7].val, sizeof(slots[7].val), "%u", sPendingRollCount);

    for (auto& sl : slots) {
        if (strncmp(tpl, sl.tag, sl.tagLen) == 0) {
            const char* v = sl.val;
            while (*v && *pDst < end) *(*pDst)++ = *v++;
            return sl.tagLen;
        }
    }
    return 0;
}

static void expand(const char* tpl, char* buf, size_t bufSize,
                   const char* s, const char* o, const char* a, uint8_t n) {
    char* dst = buf;
    const char* end = buf + bufSize - 1;

    while (*tpl && dst < end) {
        if (*tpl == '{') {
            // Try multi-char slots first
            int consumed = tryExpandMulti(tpl, &dst, end);
            if (consumed > 0) {
                tpl += consumed;
                continue;
            }
            // Single-char slots: {S} {O} {A} {N}
            if (*(tpl + 1) && *(tpl + 2) == '}') {
                const char* ins = nullptr;
                char numBuf[4];
                switch (tpl[1]) {
                    case 'S': ins = s; break;
                    case 'O': ins = o; break;
                    case 'A': ins = a; break;
                    case 'N':
                        snprintf(numBuf, sizeof(numBuf), "%d", n);
                        ins = numBuf;
                        break;
                }
                if (ins) {
                    while (*ins && dst < end) *dst++ = *ins++;
                    tpl += 3;
                    continue;
                }
            }
        }
        *dst++ = *tpl++;
    }
    *dst = '\0';
}

// ============================================================
// GENERATE — creates a new line and scrolls buffer
// ============================================================
static void generateLine(const char* const* tpls, uint8_t tplCount) {
    // Pick template, avoid immediate repeat
    uint8_t tidx;
    do {
        tidx = esp_random() % tplCount;
    } while (tidx == sLastTplIdx && tplCount > 1);
    sLastTplIdx = tidx;

    // Pick 2 unique nouns
    uint8_t si = esp_random() % NOUN_COUNT;
    uint8_t oi;
    do { oi = esp_random() % NOUN_COUNT; } while (oi == si);

    const char* adj = ADJECTIVES[esp_random() % ADJ_COUNT];
    uint8_t roll = 1 + (esp_random() % 20);

    // Scroll: line1 → line2 → line3, generate new line1
    memcpy(sLine3, sLine2, sizeof(sLine2));
    memcpy(sLine2, sLine1, sizeof(sLine1));
    expand(tpls[tidx], sLine1, sizeof(sLine1), NOUNS[si], NOUNS[oi], adj, roll);

    sLastFlashMs = millis();
    sReady = true;
}

// ============================================================
// D20 ROLL NARRATIVE — generates line from pending rolls
// ============================================================
static void generateD20Line() {
    if (sPendingRollCount == 0) return;

    const char* const* tpls;
    uint8_t tplCount;
    bool hasCrit = (sBestRoll == 20);

    if (sPendingRollCount == 1) {
        // Single roll
        if (sLastRoll == 20) {
            tpls = TPL_D20_CRIT;
            tplCount = sizeof(TPL_D20_CRIT) / sizeof(TPL_D20_CRIT[0]);
        } else if (sLastRoll == 1) {
            tpls = TPL_D20_FUMBLE;
            tplCount = sizeof(TPL_D20_FUMBLE) / sizeof(TPL_D20_FUMBLE[0]);
        } else {
            tpls = TPL_D20;
            tplCount = sizeof(TPL_D20) / sizeof(TPL_D20[0]);
        }
    } else {
        // Batch
        if (hasCrit) {
            tpls = TPL_D20_BATCH_CRIT;
            tplCount = sizeof(TPL_D20_BATCH_CRIT) / sizeof(TPL_D20_BATCH_CRIT[0]);
        } else {
            tpls = TPL_D20_BATCH;
            tplCount = sizeof(TPL_D20_BATCH) / sizeof(TPL_D20_BATCH[0]);
        }
    }

    generateLine(tpls, tplCount);

    // Reset accumulator
    sPendingRollCount = 0;
    sLastRoll = 0;
    sBestRoll = 0;
    sTotalRollXP = 0;
    sFirstRollMs = 0;
}

// ============================================================
// EVENT NARRATIVE — generates line from pending event
// ============================================================
static void generateEventLine() {
    const char* const* tpls;
    uint8_t tplCount;

    switch (sPendingEvent) {
        case EVT_HANDSHAKE:
        case EVT_PMKID:
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
            break;
        case EVT_DEAUTH_CRIT:
            tpls = TPL_D20_CRIT;
            tplCount = sizeof(TPL_D20_CRIT) / sizeof(TPL_D20_CRIT[0]);
            break;
        case EVT_DEAUTH_FUMBLE:
            tpls = TPL_D20_FUMBLE;
            tplCount = sizeof(TPL_D20_FUMBLE) / sizeof(TPL_D20_FUMBLE[0]);
            break;
        case EVT_LOW_BATTERY:
        case EVT_GPS_LOCK:
            tpls = TPL_AWARE;
            tplCount = sizeof(TPL_AWARE) / sizeof(TPL_AWARE[0]);
            break;
        default:
            sPendingEvent = EVT_NONE;
            return;
    }

    sPendingEvent = EVT_NONE;
    generateLine(tpls, tplCount);
}

// ============================================================
// UPDATE — generates new narrative line on timer or event
// ============================================================
void NarrativeEngine::update(uint8_t mode) {
    uint32_t now = millis();

    // Priority 1: D20 rolls ready (batch wait elapsed)
    if (sPendingRollCount > 0 && (now - sFirstRollMs >= ROLL_BATCH_WAIT_MS)) {
        generateD20Line();
        sLastUpdate = now;
        return;
    }

    // Priority 2: Forced event (bypass timer)
    if (sPendingEvent != EVT_NONE) {
        generateEventLine();
        sLastUpdate = now;
        return;
    }

    // Priority 3: Timer-based generation
    if (sReady && (now - sLastUpdate < UPDATE_INTERVAL_MS)) return;
    sLastUpdate = now;

    // --- Context selection ---
    // 10% lore, 15% aware (real data), then mode-driven
    const char* const* tpls;
    uint8_t tplCount;
    uint8_t ctxRoll = esp_random() % 100;

    if (ctxRoll < 10) {
        tpls = TPL_LORE;
        tplCount = sizeof(TPL_LORE) / sizeof(TPL_LORE[0]);
    } else if (ctxRoll < 25) {
        tpls = TPL_AWARE;
        tplCount = sizeof(TPL_AWARE) / sizeof(TPL_AWARE[0]);
    } else if (mode >= 1 && mode <= 3) {
        // Active modes: OINK(1), DNH(2), WARHOG(3)
        uint8_t sub = esp_random() % 100;
        if (sub < 50) {
            tpls = TPL_HUNT;
            tplCount = sizeof(TPL_HUNT) / sizeof(TPL_HUNT[0]);
        } else if (sub < 80) {
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
        } else {
            tpls = TPL_FAIL;
            tplCount = sizeof(TPL_FAIL) / sizeof(TPL_FAIL[0]);
        }
    } else {
        // Idle, menu, charging, etc.
        uint8_t sub = esp_random() % 100;
        if (sub < 65) {
            tpls = TPL_IDLE;
            tplCount = sizeof(TPL_IDLE) / sizeof(TPL_IDLE[0]);
        } else if (sub < 85) {
            tpls = TPL_FAIL;
            tplCount = sizeof(TPL_FAIL) / sizeof(TPL_FAIL[0]);
        } else {
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
        }
    }

    generateLine(tpls, tplCount);
}

// ============================================================
// EVENT + D20 PUBLIC API
// ============================================================

void NarrativeEngine::pushEvent(NarrativeEvent evt) {
    sPendingEvent = evt;  // Newest wins
}

void NarrativeEngine::pushD20Roll(uint8_t roll, uint16_t xp) {
    if (sPendingRollCount == 0) {
        sFirstRollMs = millis();
    }
    if (sPendingRollCount < 250) sPendingRollCount++;
    sLastRoll = roll;
    if (roll > sBestRoll) sBestRoll = roll;
    if (sTotalRollXP <= 60000) sTotalRollXP += xp;
}

// ============================================================
// ACCESSORS
// ============================================================

const char* NarrativeEngine::getLine1() { return sLine1; }
const char* NarrativeEngine::getLine2() { return sLine2; }
const char* NarrativeEngine::getLine3() { return sLine3; }
bool NarrativeEngine::hasContent() { return sReady; }
bool NarrativeEngine::isFlashing() { return sLastFlashMs > 0 && (millis() - sLastFlashMs) < 500; }
