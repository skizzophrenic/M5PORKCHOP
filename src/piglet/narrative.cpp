#include "narrative.h"
#include <esp_random.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

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

// ============================================================
// STATE
// ============================================================
static char sLine1[54] = "";
static char sLine2[54] = "";
static uint32_t sLastUpdate = 0;
static constexpr uint32_t UPDATE_INTERVAL_MS = 9000;
static uint8_t sLastTplIdx = 255;
static bool sReady = false;

// ============================================================
// SLOT EXPANDER — replaces {S},{O},{A},{N} in template
// ============================================================
static void expand(const char* tpl, char* buf, size_t bufSize,
                   const char* s, const char* o, const char* a, uint8_t n) {
    char* dst = buf;
    const char* end = buf + bufSize - 1;

    while (*tpl && dst < end) {
        if (*tpl == '{' && *(tpl + 1) && *(tpl + 2) == '}') {
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
        *dst++ = *tpl++;
    }
    *dst = '\0';
}

// ============================================================
// UPDATE — generates new narrative line on timer
// ============================================================
void NarrativeEngine::update(uint8_t mode) {
    uint32_t now = millis();
    if (sReady && (now - sLastUpdate < UPDATE_INTERVAL_MS)) return;
    sLastUpdate = now;

    // --- Context selection ---
    // 12% lore always, then mode-driven
    const char* const* tpls;
    uint8_t tplCount;
    uint8_t ctxRoll = esp_random() % 100;

    if (ctxRoll < 12) {
        tpls = TPL_LORE;
        tplCount = sizeof(TPL_LORE) / sizeof(TPL_LORE[0]);
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

    // --- Pick template, avoid immediate repeat ---
    uint8_t tidx;
    do {
        tidx = esp_random() % tplCount;
    } while (tidx == sLastTplIdx && tplCount > 1);
    sLastTplIdx = tidx;

    // --- Pick 2 unique nouns ---
    uint8_t si = esp_random() % NOUN_COUNT;
    uint8_t oi;
    do { oi = esp_random() % NOUN_COUNT; } while (oi == si);

    const char* adj = ADJECTIVES[esp_random() % ADJ_COUNT];
    uint8_t roll = 1 + (esp_random() % 20);

    // --- Scroll: line1 → line2, generate new line1 ---
    memcpy(sLine2, sLine1, sizeof(sLine1));
    expand(tpls[tidx], sLine1, sizeof(sLine1), NOUNS[si], NOUNS[oi], adj, roll);

    sReady = true;
}

const char* NarrativeEngine::getLine1() { return sLine1; }
const char* NarrativeEngine::getLine2() { return sLine2; }
bool NarrativeEngine::hasContent() { return sReady; }
