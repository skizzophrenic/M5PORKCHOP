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
// NARRATIVE ENGINE — algorithmic rhyming verse generator
// Rhyme families × templates × adjectives × d20 rolls = millions
// of unique RHYMING lines from ~8KB flash. The pig is a bard.
// ============================================================

// --- Rhyme families (19 families, ~89 words) ---
// Words within each family rhyme. Pick S and O from same family
// and any template placing both produces algorithmic verse.
// Each family mixes pig/farm, D&D, tech, and comedy contexts.
struct RhymeFamily {
    const char* const* words;
    uint8_t count;
};

static const char* const RF_PIG_W[]    = { "PIG", "JIG", "DIG", "RIG", "SWIG", "TWIG" };
static const char* const RF_HOG_W[]    = { "HOG", "BOG", "FOG", "LOG", "FROG", "GROG" };
static const char* const RF_SNOUT_W[]  = { "SNOUT", "CLOUT", "TROUT", "SHOUT", "SCOUT", "STOUT" };
static const char* const RF_ALE_W[]    = { "ALE", "TALE", "GRAIL", "SNAIL", "WAIL", "HAIL", "TAIL" };
static const char* const RF_PORK_W[]   = { "PORK", "FORK", "CORK", "ORC", "STORK" };
static const char* const RF_HEAP_W[]   = { "HEAP", "DEEP", "SLEEP", "CREEP", "REAP", "SHEEP" };
static const char* const RF_KNIGHT_W[] = { "KNIGHT", "BITE", "FIGHT", "MIGHT", "SIGHT", "NIGHT" };
static const char* const RF_QUEST_W[]  = { "QUEST", "REST", "BEST", "NEST", "CHEST", "TEST", "PEST" };
static const char* const RF_STACK_W[]  = { "STACK", "HACK", "PACK", "TRACK", "CRACK", "SNACK" };
static const char* const RF_MUD_W[]    = { "MUD", "THUD", "STUD", "CRUD", "BLOOD", "FLOOD" };
static const char* const RF_BONE_W[]   = { "BONE", "TONE", "THRONE", "STONE", "MOAN", "GROAN" };
static const char* const RF_LOOT_W[]   = { "LOOT", "BOOT", "HOOT", "ROOT", "BRUTE", "SNOOT" };
static const char* const RF_HAM_W[]    = { "HAM", "SPAM", "JAM", "RAM", "SCAM", "SLAM", "CLAM" };
static const char* const RF_CHOP_W[]   = { "CHOP", "DROP", "STOP", "SHOP", "CROP", "FLOP", "SLOP" };
static const char* const RF_FEAST_W[]  = { "FEAST", "BEAST", "LEAST", "PRIEST", "YEAST", "EAST" };
static const char* const RF_SPELL_W[]  = { "SPELL", "HELL", "BELL", "FELL", "WELL", "SHELL" };
static const char* const RF_GOLD_W[]   = { "GOLD", "BOLD", "OLD", "COLD", "TOLD", "MOLD" };
static const char* const RF_FLAME_W[]  = { "FLAME", "GAME", "NAME", "BLAME", "SHAME", "FAME" };
static const char* const RF_BREW_W[]   = { "BREW", "STEW", "DEW", "CREW", "FEW", "KNEW" };

static const RhymeFamily RHYME_FAMILIES[] = {
    { RF_PIG_W,    6 },
    { RF_HOG_W,    6 },
    { RF_SNOUT_W,  6 },
    { RF_ALE_W,    7 },
    { RF_PORK_W,   5 },
    { RF_HEAP_W,   6 },
    { RF_KNIGHT_W, 6 },
    { RF_QUEST_W,  7 },
    { RF_STACK_W,  6 },
    { RF_MUD_W,    6 },
    { RF_BONE_W,   6 },
    { RF_LOOT_W,   6 },
    { RF_HAM_W,    7 },
    { RF_CHOP_W,   7 },
    { RF_FEAST_W,  6 },
    { RF_SPELL_W,  6 },
    { RF_GOLD_W,   6 },
    { RF_FLAME_W,  6 },
    { RF_BREW_W,   6 },
};
static constexpr uint8_t FAMILY_COUNT = 19;

// --- Lore noun pool (flat, for non-rhyming LORE/AWARE templates) ---
static const char* const LORE_NOUNS[] = {
    "PIG",    "HORSE",  "BARN",   "BARD",    "WITCH",
    "GRAIL",  "PARROT", "COCONUT","DRAGON",  "QUEST",
    "DUCK",   "SWALLOW","HEAP",   "STACK",   "JTAG",
    "GOBLIN", "TAVERN", "MALLOC", "ROGUE",   "TROUGH"
};
static constexpr uint8_t LORE_NOUN_COUNT = 20;

// --- Adjective pool (35 words — D&D + pork preparation) ---
static const char* const ADJECTIVES[] = {
    "FERAL",    "HOLY",     "CURSED",   "HAUNTED",
    "ANCIENT",  "UNDEAD",   "SACRED",   "ARCANE",
    "BLESSED",  "ELDRITCH", "VOLATILE", "HOSTILE",
    "TACTICAL", "SENTIENT", "CRISPY",   "SMOKED",
    "BRINED",   "PICKLED",  "CURED",    "SIZZLING",
    "MYTHIC",   "CHAOTIC",  "SPECTRAL", "PRIMAL",
    "DUBIOUS",  "SKETCHY",  "DODGY",    "SUS",
    "UNHINGED", "FOUL",     "ROTISSERIE","GLAZED",
    "BRAISED",  "CHARRED",  "MARINATED"
};
static constexpr uint8_t ADJ_COUNT = 35;

// ============================================================
// TEMPLATE CATEGORIES
// Slots: {S}=subject noun, {O}=object noun,
//        {A}=adjective, {N}=d20 roll (1-20)
// ALL templates verified <=52 chars at worst case
// IDLE/HUNT/WIN/FAIL use rhyme families (S and O rhyme)
// LORE/AWARE use flat lore nouns (no rhyme needed)
// ============================================================

// --- IDLE: Bard's tavern verse, ALL rhyme via family ---
static const char* const TPL_IDLE[] = {
    "THE {A} {S} ONCE MET A {O}.",
    "THE BARD SINGS OF {S} AND ALSO {O}.",
    "THE {A} {S} DANCED A {O} ALL NIGHT.",
    "NO {S} NO {O}. SUCH IS THE WAY.",
    "BEHOLD THE {S}! BEWARE THE {O}!",
    "IN TALES OF {S} ONE FINDS THE {O}.",
    "A {S} WALKS INTO A BAR. SEES A {O}.",
    "THE {S} SITS IN THE {O}. ALL IS WELL.",
    "ODE TO {S}: THOU ART LIKE A {O}.",
    "ONCE UPON A {S} THERE WAS A {O}.",
    "THE INNKEEPER WARNS OF {A} {S} AND {O}.",
    "A TOAST! TO {S} AND {A} {O}!",
    "'{S}?' QUOTH THE {O}. 'NEVER.'",
    "THE {S} PONDERS THE {A} {O}.",
    "THE {A} {S} SEEKS A WORTHY {O}.",
    "{S} AND {O} ARGUE AT THE TAVERN.",
    "A {S} A {O} AND A BARD WALK IN.",
    "THE BALLAD OF {S} AND {O} BEGINS.",
    "THE {S} ORDERS {A} {O} AT THE BAR.",
    "TWO {S}S WALK INTO A {O}. OUCH.",
    "THE TAVERN SERVES {A} {O}. ITS FINE.",
    "THE {S} LOST ITS {O} LAST TUESDAY.",
    "RUMOR: THE {O} IS ACTUALLY {A}.",
    "THE INNKEEPER SELLS {A} {S} CHEAP.",
    "A {A} {S} REVIEWS THE {O}. ONE STAR.",
    "THE {S} NAPS. DREAMS OF {A} {O}.",
};

// --- HUNTING: D&D encounter rhyming verse ---
static const char* const TPL_HUNT[] = {
    "{S} STALKS THE {A} {O}. ROLL {N}.",
    "A WANDERING {S} BEFRIENDS A {O}.",
    "THE {A} {S} HUNTS. THE {O} HIDES.",
    "{S} SNIFFS. THERE IS {A} {O} NEAR.",
    "THE {O} LURKS. {S} ROLLS {N}.",
    "{S} AND {O} CIRCLE. ROLL FOR INIT.",
    "ENCOUNTER: {A} {S}. ALSO A {O}.",
    "THE {S} TRACKS {O} THROUGH THE {A} MIRE.",
    "{S} ENTERS THE LAIR. FINDS A {O}.",
    "{S} SEARCHES FOR {O}. ROLL: {N}.",
    "THE {A} {O} ELUDES THE {S}. FOR NOW.",
    "{S} READIES FOR {O}. WEAPON DRAWN.",
    "SOMETHING {A} STIRS. {S} SEES {O}.",
    "{S} CHECKS FOR {O}. PERCEPTION: {N}.",
    "{S} PICKS UP THE SCENT OF {A} {O}.",
    "THE HUNT FOR {A} {O} BEGINS. AGAIN.",
    "AMBUSH! {S} WASNT READY FOR {O}.",
    "THE {A} {O} LEAVES TRACKS. {S} FOLLOWS.",
    "{S} SETS A TRAP BAITED WITH {O}.",
    "STEALTH CHECK: {S} VS {O}. ROLL {N}.",
};

// --- VICTORY: Triumph, epic rhyming ---
static const char* const TPL_WIN[] = {
    "THE {O} HAS FALLEN TO THE {A} {S}!",
    "{S} CRITS THE {O}! PRAISE THE SUN.",
    "QUEST DONE. THE {S} LOOTS THE {O}.",
    "{S} VANQUISHES {O}. BARD TAKES NOTES.",
    "VICTORY! {S} CLAIMS THE {A} {O}!",
    "{S} DEVOURS THE {A} {O}. DELICIOUS.",
    "THE {A} {S} STANDS OVER THE {O}.",
    "NAT {N}! {S} DESTROYS THE {O}.",
    "{O} DROPS. {S} GAINS {A} LOOT.",
    "THE {S} FEASTS UPON THE {A} {O}.",
    "{S} WINS. {O} IS SERVED {A}.",
    "THE {O} IS DONE. THE {S} PREVAILS.",
    "THE {A} {S} LOOTS 3 GOLD AND A {O}.",
    "GG. THE {O} NEVER STOOD A CHANCE.",
    "{S} FLEXES OVER THE {A} {O}. EARNED.",
    "FLAWLESS. {S} DIDNT EVEN NEED THE {O}.",
};

// --- DEFEAT: Monty Python, humorous rhyming ---
static const char* const TPL_FAIL[] = {
    "TIS BUT A {S}. NAUGHT BUT A {O}.",
    "RUN AWAY! THE {A} {S} HAS A {O}!",
    "THE {S} IS DEAD. LONG LIVE THE {O}.",
    "NONE SHALL PASS. ASK THE {A} {S}.",
    "{S} FLED. THE {O} DIDNT NOTICE.",
    "THE {S} OBJECTS. THE {O} AGREES.",
    "BRING OUT YOUR {S}. ALSO YOUR {O}.",
    "{S} TAKES AN ARROW TO THE {O}.",
    "YOUR {S} DIED. {O} SHRUGS.",
    "TPK. BLAME THE {S}. AND THE {O}.",
    "THE {A} {S} HAS CEASED TO BE A {O}.",
    "{S} ROLLED {N}. CRIT FAIL. OH {O}.",
    "THE {S} TRIED. THE {O} DIDNT CARE.",
    "EVERYONE SAW {S} FAIL. EVEN THE {O}.",
    "THE {A} {S} RAGE QUITS. {O} WAVES.",
    "SKILL ISSUE. THE {O} WAS JUST A {S}.",
};

// --- LORE: porkchop mythology (uses lore nouns, no rhyme) ---
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
    "THE {S} HAS A CUNNING PLAN ABOUT {O}.",
    "ON SECOND THOUGHT LETS NOT GO TO {O}.",
    "THERE IS NO {S} IN THIS {O}. TRUST.",
    "THE {S} PROTOCOL HAS BEEN DEPRECATED.",
    "LEGEND SAYS THE FIRST {S} COMPILED ITSELF.",
    "THE {S} WAS INSIDE THE {O} ALL ALONG.",
    "PATCH NOTES: REMOVED {S}. ADDED MORE {O}.",
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
    "SESSION: {NETS} TARGETS. {HS} CAUGHT. {A}.",
    "{BAT}% POWER. {NETS} REMAIN. THE {S} PUSHES.",
    "LVL {LVL} {S}. {HS} TROPHIES MOUNTED.",
    "CH{CH} HUMS WITH {A} ENERGY. {NETS} SOULS.",
    "{NETS} IN THE AIR. {HS} IN THE BAG.",
    "THE {S} HAS SEEN {NETS} REALMS TODAY.",
};

// --- TAUNT: directed at visible networks (uses {SSID}, lore nouns) ---
static const char* const TPL_TAUNT[] = {
    "'{SSID}' HAS NO IDEA WHATS COMING.",
    "THE {A} {S} CIRCLES '{SSID}'. PATIENCE.",
    "'{SSID}': YOUR PMF WONT SAVE YOU.",
    "THE BARD WRITES AN ODE TO '{SSID}'.",
    "'{SSID}' LOOKS {A}. THE {S} DISAGREES.",
    "WHO NAMED THEIR WIFI '{SSID}'? WHY.",
    "'{SSID}' BROADCASTS FEAR. THE {S} SMELLS IT.",
    "THE {S} JUDGES '{SSID}'. FOUND WANTING.",
    "'{SSID}' SLEEPS. THE {A} {S} DOES NOT.",
    "DEAR '{SSID}': THE {S} SENDS REGARDS.",
};

// --- QUIET: existential, when no networks visible (rhyme families) ---
static const char* const TPL_QUIET[] = {
    "THE AIR IS DEAD. NOT A {S} STIRS.",
    "THE {A} {S} LISTENS. SILENCE. THEN {O}.",
    "NOTHING ON ANY CHANNEL. THE {S} WAITS.",
    "NO TARGETS. THE {A} {S} QUESTIONS LIFE.",
    "THE SPECTRUM IS A GRAVEYARD OF {O}.",
    "THE {S} HOWLS INTO THE VOID. NO {O}.",
    "CHANNEL {CH}: EMPTY. THE {S} SIGHS.",
    "SOMEWHERE A {O} EXISTS. NOT HERE.",
};

// --- META: 4th wall breaks, self-aware humor (rhyme families) ---
static const char* const TPL_META[] = {
    "THE {S} SUSPECTS IT LIVES IN A DEVICE.",
    "WAIT. IS ANYONE READING THIS.",
    "THIS NARRATIVE HAS NO PLOT. ONLY {S}.",
    "THE BARD FORGETS THE LINE. BLAMES {O}.",
    "PAGE {N}. THE SCROLL HAS NO END.",
    "THE {A} {S} BREAKS THE FOURTH WALL.",
    "SOMEWHERE A DEV LAUGHS AT THIS.",
    "THE {S} WONDERS WHY ITS ALWAYS UPPER CASE.",
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

// Verse continuation — track last rhyme family for rolling verse
static int8_t sLastRhymeFamily = -1;

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
    struct Slot { const char* tag; int tagLen; char val[16]; };
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
        { "{SSID}",  6, "" },
    };
    snprintf(slots[0].val, sizeof(slots[0].val), "%u", nets);
    snprintf(slots[1].val, sizeof(slots[1].val), "%u", hs);
    snprintf(slots[2].val, sizeof(slots[2].val), "%u", ch);
    snprintf(slots[3].val, sizeof(slots[3].val), "%d", bat);
    snprintf(slots[4].val, sizeof(slots[4].val), "%u", lvl);
    snprintf(slots[5].val, sizeof(slots[5].val), "%u", sLastRoll);
    snprintf(slots[6].val, sizeof(slots[6].val), "%u", sTotalRollXP);
    snprintf(slots[7].val, sizeof(slots[7].val), "%u", sPendingRollCount);
    // {SSID}: pick a random visible network name
    const auto& netVec = NetworkRecon::getNetworks();
    if (!netVec.empty()) {
        uint16_t pick = esp_random() % netVec.size();
        strncpy(slots[8].val, netVec[pick].ssid, 14);
        slots[8].val[14] = '\0';
        for (char* p = slots[8].val; *p; p++) *p = toupper(*p);
        // Fall back if SSID was empty (hidden network)
        if (slots[8].val[0] == '\0') strcpy(slots[8].val, "HIDDEN");
    } else {
        strcpy(slots[8].val, "UNKNOWN");
    }

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
// useRhyme: true = pick from rhyme family (S/O rhyme)
//           false = pick from flat LORE_NOUNS (no rhyme)
// ============================================================
static void generateLine(const char* const* tpls, uint8_t tplCount, bool useRhyme) {
    // Pick template, avoid immediate repeat
    uint8_t tidx;
    do {
        tidx = esp_random() % tplCount;
    } while (tidx == sLastTplIdx && tplCount > 1);
    sLastTplIdx = tidx;

    const char* sWord;
    const char* oWord;

    if (useRhyme) {
        // --- Rhyme family selection with verse continuation ---
        uint8_t fi;
        // 30% chance to reuse last family for rolling verse effect
        if (sLastRhymeFamily >= 0 && (esp_random() % 100) < 30) {
            fi = (uint8_t)sLastRhymeFamily;
        } else {
            fi = esp_random() % FAMILY_COUNT;
        }
        sLastRhymeFamily = (int8_t)fi;

        const RhymeFamily& fam = RHYME_FAMILIES[fi];
        // Pick 2 unique words from the same family
        uint8_t si = esp_random() % fam.count;
        uint8_t oi;
        do { oi = esp_random() % fam.count; } while (oi == si);
        sWord = fam.words[si];
        oWord = fam.words[oi];
    } else {
        // --- Flat lore noun pick (no rhyme needed) ---
        uint8_t si = esp_random() % LORE_NOUN_COUNT;
        uint8_t oi;
        do { oi = esp_random() % LORE_NOUN_COUNT; } while (oi == si);
        sWord = LORE_NOUNS[si];
        oWord = LORE_NOUNS[oi];
    }

    const char* adj = ADJECTIVES[esp_random() % ADJ_COUNT];
    uint8_t roll = 1 + (esp_random() % 20);

    // Scroll: line1 → line2 → line3, generate new line1
    memcpy(sLine3, sLine2, sizeof(sLine2));
    memcpy(sLine2, sLine1, sizeof(sLine1));
    expand(tpls[tidx], sLine1, sizeof(sLine1), sWord, oWord, adj, roll);

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

    generateLine(tpls, tplCount, true);

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
    bool useRhyme = true;

    switch (sPendingEvent) {
        case EVT_HANDSHAKE:
        case EVT_PMKID:
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
            break;
        case EVT_LOW_BATTERY:
        case EVT_GPS_LOCK:
            tpls = TPL_AWARE;
            tplCount = sizeof(TPL_AWARE) / sizeof(TPL_AWARE[0]);
            useRhyme = false;  // AWARE uses lore nouns
            break;
        default:
            sPendingEvent = EVT_NONE;
            return;
    }

    sPendingEvent = EVT_NONE;
    generateLine(tpls, tplCount, useRhyme);
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

    // --- Context-aware selection ---
    // Situational overrides first, then standard weighted selection
    const char* const* tpls;
    uint8_t tplCount;
    bool useRhyme;
    uint8_t ctxRoll = esp_random() % 100;

    uint16_t netCount = NetworkRecon::getNetworkCount();
    int bat = M5.Power.getBatteryLevel();
    uint32_t sessionSec = (now - XP::getSession().startTime) / 1000;

    // === CONTEXT OVERRIDES (checked first) ===
    if (netCount == 0 && ctxRoll < 40) {
        // No networks → existential QUIET templates
        tpls = TPL_QUIET;
        tplCount = sizeof(TPL_QUIET) / sizeof(TPL_QUIET[0]);
        useRhyme = true;
    } else if (bat > 0 && bat < 20 && ctxRoll < 30) {
        // Low battery → urgent AWARE templates
        tpls = TPL_AWARE;
        tplCount = sizeof(TPL_AWARE) / sizeof(TPL_AWARE[0]);
        useRhyme = false;
    } else if (netCount > 0 && ctxRoll < 12) {
        // Networks visible → TAUNT a specific SSID
        tpls = TPL_TAUNT;
        tplCount = sizeof(TPL_TAUNT) / sizeof(TPL_TAUNT[0]);
        useRhyme = false;
    } else if (sessionSec > 1800 && ctxRoll < 7) {
        // Long session (30+ min) → 4th wall META
        tpls = TPL_META;
        tplCount = sizeof(TPL_META) / sizeof(TPL_META[0]);
        useRhyme = true;
    }
    // === STANDARD SELECTION ===
    else if (ctxRoll < 17) {
        tpls = TPL_LORE;
        tplCount = sizeof(TPL_LORE) / sizeof(TPL_LORE[0]);
        useRhyme = false;
    } else if (ctxRoll < 37) {
        tpls = TPL_AWARE;
        tplCount = sizeof(TPL_AWARE) / sizeof(TPL_AWARE[0]);
        useRhyme = false;
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
        useRhyme = true;
    } else if (mode == 4) {
        // PIGGYBLUES: BLE notification spam — predatory scanning flavor
        uint8_t sub = esp_random() % 100;
        if (sub < 50) {
            tpls = TPL_HUNT;
            tplCount = sizeof(TPL_HUNT) / sizeof(TPL_HUNT[0]);
            useRhyme = true;
        } else if (sub < 80) {
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
            useRhyme = true;
        } else {
            tpls = TPL_TAUNT;
            tplCount = sizeof(TPL_TAUNT) / sizeof(TPL_TAUNT[0]);
            useRhyme = false;  // TAUNT uses lore nouns / SSIDs
        }
    } else if (mode == 21) {
        // BACON: beacon broadcaster — hide-and-seek deception
        uint8_t sub = esp_random() % 100;
        if (sub < 40) {
            tpls = TPL_HUNT;
            tplCount = sizeof(TPL_HUNT) / sizeof(TPL_HUNT[0]);
            useRhyme = true;
        } else if (sub < 70) {
            tpls = TPL_LORE;
            tplCount = sizeof(TPL_LORE) / sizeof(TPL_LORE[0]);
            useRhyme = false;
        } else if (sub < 90) {
            tpls = TPL_META;
            tplCount = sizeof(TPL_META) / sizeof(TPL_META[0]);
            useRhyme = true;
        } else {
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
            useRhyme = true;
        }
    } else {
        // Idle, menu, charging, etc.
        uint8_t sub = esp_random() % 100;
        if (sub < 60) {
            tpls = TPL_IDLE;
            tplCount = sizeof(TPL_IDLE) / sizeof(TPL_IDLE[0]);
        } else if (sub < 80) {
            tpls = TPL_FAIL;
            tplCount = sizeof(TPL_FAIL) / sizeof(TPL_FAIL[0]);
        } else {
            tpls = TPL_WIN;
            tplCount = sizeof(TPL_WIN) / sizeof(TPL_WIN[0]);
        }
        useRhyme = true;
    }

    generateLine(tpls, tplCount, useRhyme);
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
