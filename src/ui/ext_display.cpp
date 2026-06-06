#include "ext_display.h"
#include "display.h"                 // DISPLAY_W/H, TOP_BAR_H, BOTTOM_BAR_H
#include "../modes/oink.h"           // DetectedNetwork
#include "../core/network_recon.h"   // NetworkRecon::getNetworks()
#include <math.h>
#include <M5Unified.h>   // M5.Mic / M5.Speaker (audio-reactive)

// ---------------- Feature toggles ----------------
static constexpr bool MATRIX_RICH = false;  // true: 16-bit TFT (smooth, slower)
                                            // false: 8-bit TFT (fast)
static constexpr bool AUDIO_REACTIVE = true; // mic-driven menu fx (mic only on in menu;
                                             // pauses speaker SFX there). Set false to disable.

// Band geometry (content scales to a centered 320x180 strip; bands are the rest)
static constexpr int SCR_W   = 320;
static constexpr int SCR_H   = 240;
static constexpr int BAND_H  = 30;
static constexpr int TOP_Y   = 0;
static constexpr int BOT_Y   = SCR_H - BAND_H;   // 210

LGFX_ExtILI9341 extDisplay;

// (Scale buffer removed to reclaim ~32KB: strips are scaled straight to the panel.)

// ---------------- Effect selection ----------------
enum BandFx : uint8_t { FX_MATRIX = 0, FX_TICKER, FX_SPECTRUM, FX_FIRE,
                        FX_STARFIELD, FX_PLASMA, FX_SUNSET, FX_TOASTERS, FX_SQUACHCAM, FX_OFF, FX_COUNT };
uint8_t extBandEffect = FX_MATRIX;
static bool fxDirty = true;          // clear bands on next draw after a switch

void extCycleBandEffect(int dir) {
    extBandEffect = (uint8_t)((extBandEffect + dir + FX_COUNT) % FX_COUNT);
    fxDirty = true;
}

static void bandsClear() {
    extDisplay.fillRect(0, TOP_Y, SCR_W, BAND_H, TFT_BLACK);
    extDisplay.fillRect(0, BOT_Y, SCR_W, BAND_H, TFT_BLACK);
}
static uint16_t hsv565(uint16_t h, uint8_t s, uint8_t v);  // defined below

// ---------------- Shared cached scan data (refreshed ~1/sec) ----------------
static char     tickerStr[260];
static int8_t   chanMax[14];         // index 1..13 = best RSSI on that channel
static uint32_t dataLast = 0;

static void refreshScanData() {
    uint32_t now = millis();
    if (now - dataLast < 1000) return;
    dataLast = now;

    for (int i = 0; i < 14; i++) chanMax[i] = -127;
    tickerStr[0] = '\0';
    size_t pos = 0;
    NetworkRecon::CriticalSection lock;            // RAII guard
    auto& nets = NetworkRecon::getNetworks();
    int n = 0;
    for (auto& net : nets) {
        if (net.channel >= 1 && net.channel <= 13 && net.rssi > chanMax[net.channel])
            chanMax[net.channel] = net.rssi;
        if (n++ < 22 && pos < sizeof(tickerStr) - 48) {
            const char* name = (net.ssid[0]) ? net.ssid : "<hidden>";
            pos += snprintf(tickerStr + pos, sizeof(tickerStr) - pos,
                            "%s  c%u %ddBm    ", name, net.channel, net.rssi);
        }
    }
    if (tickerStr[0] == '\0')
        snprintf(tickerStr, sizeof(tickerStr), "scanning for networks ...    ");
}

// ---------------- FX_MATRIX (classic green) ----------------
#define MB_COLS 53
#define MB_ROWS 6      // trail length (rows)
#define MB_PITCH 5     // px between rows (slight overlap -> denser, taller rain)
static int8_t  mbHeadTop[MB_COLS], mbHeadBot[MB_COLS];
static uint8_t mbDlyTop[MB_COLS], mbDlyBot[MB_COLS];
static bool    mbInit = false;
static const char MBCH[] = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789@#$%&*+=<>?/";
static inline char mbRandChar() { return MBCH[random(sizeof(MBCH) - 1)]; }

static inline uint16_t mbShade(int dist) {
    if (dist <= 0) return TFT_WHITE;                       // bright head
    int v = 245 - (dist - 1) * 30; if (v < 60) v = 60;     // long fading green tail
    return hsv565(120, 255, v);
}
static void mbStepBand(int8_t* head, uint8_t* dly, int baseY) {
    for (int c = 0; c < MB_COLS; c++) {
        if (dly[c] > 0) { dly[c]--; head[c] = -1; }
        else { head[c]++; if (head[c] > MB_ROWS + 1) { dly[c] = 2 + random(14); head[c] = -1; } }
        int x = c * 6;
        for (int r = 0; r < MB_ROWS; r++) {
            int dist = head[c] - r;
            int y = baseY + r * MB_PITCH;
            if (dist < 0 || dist >= MB_ROWS) {                 // off: paint black directly
                extDisplay.fillRect(x, y, 6, MB_PITCH, TFT_BLACK);
                continue;
            }
            extDisplay.setTextColor(mbShade(dist), TFT_BLACK); // opaque bg = full repaint
            extDisplay.setCursor(x, y);
            extDisplay.print(mbRandChar());
        }
    }
}
static void fxMatrix() {
    if (!mbInit) {
        for (int c = 0; c < MB_COLS; c++) {
            mbHeadTop[c] = -(int8_t)random(MB_ROWS + 8); mbHeadBot[c] = -(int8_t)random(MB_ROWS + 8);
            mbDlyTop[c] = random(10); mbDlyBot[c] = random(10);
        }
        mbInit = true;
    }
    extDisplay.setTextSize(1);                   // full-cover per cell -> no band wipe
    mbStepBand(mbHeadTop, mbDlyTop, TOP_Y + 1);
    mbStepBand(mbHeadBot, mbDlyBot, BOT_Y + 1);
}

// ---------------- FX_TICKER (scrolling AP list) ----------------
static int tickerX = SCR_W;
static void fxTicker() {
    refreshScanData();
    int w = extDisplay.textWidth(tickerStr);
    if (w < 1) w = 1;
    tickerX -= 4;
    if (tickerX < -w) tickerX = SCR_W;
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(TFT_GREEN, TFT_BLACK);
    extDisplay.setTextDatum(TL_DATUM);
    // repaint just the text rows (full width) so the rest of the band never flashes
    extDisplay.fillRect(0, TOP_Y + 9, SCR_W, 12, TFT_BLACK);
    extDisplay.fillRect(0, BOT_Y + 9, SCR_W, 12, TFT_BLACK);
    extDisplay.drawString(tickerStr, tickerX, TOP_Y + 11);
    extDisplay.drawString(tickerStr, SCR_W - tickerX - w, BOT_Y + 11);
}

// ---------------- FX_SPECTRUM (animated channel analyzer) ----------------
static uint32_t chanLast = 0;
static void refreshChan() {                  // faster + lighter than the ticker refresh
    uint32_t now = millis();
    if (now - chanLast < 300) return;
    chanLast = now;
    for (int i = 0; i < 14; i++) chanMax[i] = -127;
    NetworkRecon::CriticalSection lock;
    auto& nets = NetworkRecon::getNetworks();
    for (auto& net : nets)
        if (net.channel >= 1 && net.channel <= 13 && net.rssi > chanMax[net.channel])
            chanMax[net.channel] = net.rssi;
}
static float specLvl[14] = {0}, specPeak[14] = {0};
static void fxSpectrum() {
    refreshChan();
    uint8_t scanCh = NetworkRecon::getCurrentChannel();   // the scanner hops -> sweep
    float t = millis() * 0.001f;
    const int slot = SCR_W / 13;
    for (int ch = 1; ch <= 13; ch++) {
        int rssi = chanMax[ch];
        float target = 0.0f;
        if (rssi > -110) {
            int lvl = rssi + 100; if (lvl < 0) lvl = 0; if (lvl > 70) lvl = 70;
            target = (float)lvl / 70.0f * BAND_H;
            target += sinf(t * 6.0f + ch) * 1.5f;          // shimmer so bars never sit still
        }
        if (ch == scanCh && target < BAND_H * 0.9f) target = BAND_H * 0.9f;  // scan sweep pulse
        if (target < 0) target = 0; if (target > BAND_H) target = BAND_H;
        specLvl[ch] += (target - specLvl[ch]) * 0.30f;     // smooth rise/fall
        if (specLvl[ch] > specPeak[ch]) specPeak[ch] = specLvl[ch];
        else { specPeak[ch] -= 0.5f; if (specPeak[ch] < 0) specPeak[ch] = 0; }

        int h = (int)specLvl[ch], pk = (int)specPeak[ch];
        uint16_t col = (ch == scanCh) ? TFT_WHITE
                     : (rssi > -55) ? TFT_RED : (rssi > -75) ? TFT_YELLOW : TFT_GREEN;
        int x = (ch - 1) * slot + 3, bw = slot - 5;
        extDisplay.fillRect(x, TOP_Y, bw, h, col);                    // top bar grows down
        extDisplay.fillRect(x, TOP_Y + h, bw, BAND_H - h, TFT_BLACK); // + black remainder
        extDisplay.fillRect(x, BOT_Y, bw, BAND_H - h, TFT_BLACK);     // bottom: black remainder
        extDisplay.fillRect(x, BOT_Y + (BAND_H - h), bw, h, col);     // + bar grows up
        extDisplay.drawFastHLine(x, TOP_Y + pk,     bw, TFT_WHITE);   // falling peak caps
        extDisplay.drawFastHLine(x, SCR_H - 1 - pk, bw, TFT_WHITE);
    }
}

// ---------------- FX_FIRE (per-column flame tongues) ----------------
#define FIRE_COLS 80
static float fireTop[FIRE_COLS], fireBot[FIRE_COLS];
static float fireFuel[FIRE_COLS];        // per-column base height from the RF environment
static float fireEnergy = 0.2f;          // 0..1 global intensity from live packet rate
static uint32_t fireDataLast = 0, firePktLast = 0;
static bool fireInit = false;

static inline uint32_t fnv6(const uint8_t* b) {          // hash a BSSID -> column
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
// Rebuild the fuel map from real WiFi: each AP is a hot tongue at its own column
// (taller = stronger signal); overall energy tracks packet traffic.
static void fireRefresh() {
    uint32_t now = millis();
    if (now - fireDataLast < 400) return;
    float dt = (now - fireDataLast) / 1000.0f;
    fireDataLast = now;

    uint32_t pkts = NetworkRecon::getPacketCount();        // traffic -> energy
    float rate = (dt > 0.001f) ? (float)(pkts - firePktLast) / dt : 0.0f;
    firePktLast = pkts;
    float eTarget = rate / 300.0f; if (eTarget > 1.0f) eTarget = 1.0f;   // ~300 pkt/s = roaring
    fireEnergy += (eTarget - fireEnergy) * 0.4f;
    if (fireEnergy < 0.10f) fireEnergy = 0.10f;            // always a little fire

    for (int c = 0; c < FIRE_COLS; c++) fireFuel[c] *= 0.5f;   // decay old fuel
    {
        NetworkRecon::CriticalSection lock;
        auto& nets = NetworkRecon::getNetworks();
        for (auto& net : nets) {
            int c = (int)(fnv6(net.bssid) % FIRE_COLS);
            int sg = net.rssi + 100; if (sg < 0) sg = 0; if (sg > 70) sg = 70;
            float add = (BAND_H * 0.45f) * (sg / 70.0f);   // strong AP = tall tongue
            fireFuel[c] += add;
            fireFuel[(c + 1) % FIRE_COLS] += add * 0.4f;
            fireFuel[(c + FIRE_COLS - 1) % FIRE_COLS] += add * 0.4f;
        }
    }
    float floorH = BAND_H * (0.10f + 0.35f * fireEnergy);  // global bed of flame
    for (int c = 0; c < FIRE_COLS; c++) {
        fireFuel[c] += floorH;
        if (fireFuel[c] > BAND_H) fireFuel[c] = BAND_H;
    }
}
// Turbulent step: random per-column flicker + occasional sparks (both scaled by
// traffic), smoothed toward the fuel target so it dances without strobing.
static void fireStep(float* h) {
    for (int c = 0; c < FIRE_COLS; c++) {
        float flick  = (float)(random(0, 7) - 3) * (0.5f + fireEnergy);   // +/- jitter
        float target = fireFuel[c] + flick;
        if ((int)random(0, 1000) < (int)(fireEnergy * 80.0f))             // sparks on traffic
            target += (float)random(2, (int)(BAND_H * 0.7f) + 3);
        h[c] += (target - h[c]) * 0.40f;                                  // smooth -> no strobe
        if (h[c] < 0) h[c] = 0; if (h[c] > BAND_H) h[c] = BAND_H;
    }
}
static void drawFlame(int x, int bw, int baseY, int h, bool down) {
    // hot base -> cool tip; 'down' grows from top edge downward
    int hot = h * 5 / 10, mid = h * 3 / 10;   // remainder = tip
    if (down) {
        extDisplay.fillRect(x, baseY,             bw, hot,         TFT_YELLOW);
        extDisplay.fillRect(x, baseY + hot,       bw, mid,         TFT_ORANGE);
        extDisplay.fillRect(x, baseY + hot + mid, bw, h - hot - mid, TFT_RED);
    } else {
        extDisplay.fillRect(x, baseY - hot,             bw, hot,         TFT_YELLOW);
        extDisplay.fillRect(x, baseY - hot - mid,       bw, mid,         TFT_ORANGE);
        extDisplay.fillRect(x, baseY - h,               bw, h - hot - mid, TFT_RED);
    }
}
static void fxFire() {
    if (!fireInit) { for (int c = 0; c < FIRE_COLS; c++) { fireTop[c] = random(BAND_H); fireBot[c] = random(BAND_H); fireFuel[c] = BAND_H * 0.2f; } fireInit = true; }
    fireRefresh();           // pull live WiFi -> fuel map + energy
    fireStep(fireTop);       // independent randomness per band -> not mirrored
    fireStep(fireBot);
    int bw = SCR_W / FIRE_COLS;   // 4px
    for (int c = 0; c < FIRE_COLS; c++) {
        int x = c * bw;
        int ht = (int)fireTop[c], hb = (int)fireBot[c];
        extDisplay.fillRect(x, TOP_Y + ht, bw, BAND_H - ht, TFT_BLACK);  // black below top flame
        drawFlame(x, bw, TOP_Y, ht, true);                              // top licks down
        extDisplay.fillRect(x, BOT_Y, bw, BAND_H - hb, TFT_BLACK);      // black above bottom flame
        drawFlame(x, bw, SCR_H, hb, false);                            // bottom licks up
    }
}

// ---------------- HSV -> RGB565 ----------------
static uint16_t hsv565(uint16_t h, uint8_t s, uint8_t v) {
    h %= 360; uint8_t region = h / 60; uint16_t rem = (h % 60) * 255 / 60;
    uint8_t p = (uint16_t)v * (255 - s) / 255;
    uint8_t q = (uint16_t)v * (255 - (uint16_t)s * rem / 255) / 255;
    uint8_t t = (uint16_t)v * (255 - (uint16_t)s * (255 - rem) / 255) / 255;
    uint8_t r, g, b;
    switch (region) {
        case 0: r = v; g = t; b = p; break;  case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;  case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;  default: r = v; g = p; b = q; break;
    }
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ---------------- FX_STARFIELD (warp) ----------------
#define STAR_N 70
static float   starX[STAR_N];
static int16_t starY[STAR_N];
static uint8_t starSp[STAR_N];
static bool    starInit = false;
static int16_t starBandY() {                 // pick a y in either band
    return (random(2)) ? (TOP_Y + 1 + random(BAND_H - 2))
                       : (BOT_Y + 1 + random(BAND_H - 2));
}
static void fxStarfield() {
    if (!starInit) {
        for (int i = 0; i < STAR_N; i++) { starX[i] = random(SCR_W); starY[i] = starBandY(); starSp[i] = 1 + random(9); }
        starInit = true;
    }
    for (int i = 0; i < STAR_N; i++) {
        int oldLen = starSp[i] * 2;                              // erase previous streak
        extDisplay.drawFastHLine((int)starX[i] - oldLen, starY[i], oldLen + 1, TFT_BLACK);
        starX[i] += starSp[i];
        if (starX[i] > SCR_W) { starX[i] = 0; starY[i] = starBandY(); starSp[i] = 1 + random(9); }
        int len = starSp[i] * 2;
        uint8_t b = starSp[i] * 255 / 9;                         // faster = brighter
        uint16_t c = ((b & 0xF8) << 8) | ((b & 0xFC) << 3) | (b >> 3);
        extDisplay.drawFastHLine((int)starX[i] - len, starY[i], len, c);
        extDisplay.drawPixel((int)starX[i], starY[i], TFT_WHITE);  // bright head
    }
}

// ---------------- FX_PLASMA (vaporwave cyan<->magenta) ----------------
static void fxPlasma() {
    float tt = millis() * 0.002f;
    const int step = 6;
    const int bandY[2] = { TOP_Y, BOT_Y };
    for (int bi = 0; bi < 2; bi++) {
        for (int gy = 0; gy < BAND_H; gy += step) {
            int yy = bandY[bi] + gy;
            for (int gx = 0; gx < SCR_W; gx += step) {
                float v = sinf(gx * 0.06f + tt)
                        + sinf(yy * 0.10f + tt * 1.3f)
                        + sinf((gx + yy) * 0.05f + tt * 0.7f);
                float n = (v + 3.0f) / 6.0f;                      // 0..1
                uint16_t hue = 180 + (uint16_t)(n * 150);         // cyan..magenta/pink
                extDisplay.fillRect(gx, yy, step, step, hsv565(hue, 255, 255));
            }
        }
    }
}

// ---------------- FX_SUNSET (synthwave drive) ----------------
static int drvOffset = 0;                     // road dash scroll position
static void fxSunset() {                      // synthwave drive: sky up top, car on a road below
    uint32_t ms = millis();
    float t = ms * 0.001f;

    // ===== TOP band: sky =====
    for (int r = 0; r < BAND_H; r++) {                       // gradient magenta -> orange horizon
        float f = (float)r / BAND_H;
        uint16_t hue = (288 + (uint16_t)(f * 80)) % 360;
        extDisplay.drawFastHLine(0, TOP_Y + r, SCR_W, hsv565(hue, 235, 255));
    }
    for (int s2 = 0; s2 < 7; s2++) {                          // twinkling stars
        int stx = (s2 * 47 + 11) % SCR_W, sty = TOP_Y + 1 + (s2 * 3) % 5;
        if (((ms / 280 + s2) & 3) == 0) extDisplay.drawPixel(stx, sty, TFT_WHITE);
    }
    int sunX = 160 + (int)(sinf(t * 0.45f) * 46);             // drifting, bobbing sun
    int sunY = TOP_Y + 17 + (int)(sinf(t * 0.9f) * 2);
    int sunR = 11;
    for (int dy = -sunR; dy <= sunR; dy++) {
        int yy = sunY + dy;
        if (yy < TOP_Y || yy >= TOP_Y + BAND_H) continue;
        if (dy > -3 && ((yy & 1) == 0)) continue;            // horizontal slits (lower half)
        int span = (int)sqrtf((float)(sunR * sunR - dy * dy));
        float vf = (float)(dy + sunR) / (2 * sunR);
        uint16_t hue = (uint16_t)(50 - vf * 25);             // yellow top -> orange-red bottom
        extDisplay.drawFastHLine(sunX - span, yy, span * 2, hsv565(hue, 255, 255));
    }

    // ===== BOTTOM band: perspective road =====
    extDisplay.fillRect(0, BOT_Y, SCR_W, BAND_H, hsv565(265, 170, 36));  // dark ground
    const int vpx = 160;                                     // vanishing point x (horizon = BOT_Y)
    for (int r = 0; r < BAND_H; r++) {
        int yy = BOT_Y + r;
        float tt = (float)r / BAND_H;                        // 0 horizon .. 1 near
        int half = 4 + (int)(tt * 120);                      // road widens toward viewer
        extDisplay.drawFastHLine(vpx - half, yy, half * 2, hsv565(272, 55, 24 + (int)(tt * 34)));
        extDisplay.drawPixel(vpx - half, yy, TFT_CYAN);      // neon edges
        extDisplay.drawPixel(vpx + half, yy, TFT_MAGENTA);
        int dashPhase = (int)(tt * tt * 70) + drvOffset;     // perspective-spaced dashes
        if ((dashPhase / 4) & 1) {
            int dw = 1 + (int)(tt * 3);
            extDisplay.drawFastHLine(vpx - dw, yy, dw * 2, hsv565(54, 255, 255));
        }
    }
    drvOffset += 3; if (drvOffset > 1000000) drvOffset = 0;  // forward motion

    // car (rear view) near the bottom, gentle sway + bob
    int carW = 42, carH = 13;
    int carX = vpx - carW / 2 + (int)(sinf(t * 0.6f) * 12);
    int carY = SCR_H - carH - 1 + (int)(sinf(t * 6.0f) * 1.0f);
    extDisplay.fillRoundRect(carX, carY, carW, carH, 3, hsv565(312, 200, 70)); // body
    extDisplay.fillRect(carX + 5, carY + 2, carW - 10, 4, hsv565(200, 110, 130)); // rear window
    extDisplay.fillRect(carX + 2, carY + carH - 4, 7, 3, TFT_RED);             // taillights
    extDisplay.fillRect(carX + carW - 9, carY + carH - 4, 7, 3, TFT_RED);
    extDisplay.drawFastHLine(carX, carY + carH, carW, TFT_MAGENTA);            // underglow
}

// ---------------- FX_TOASTERS (After Dark homage) ----------------
#define TOAST_N 6
struct Flyer { float x; int16_t y; uint8_t spd; uint8_t kind; float flap; int16_t px; int16_t py; };
static Flyer flyers[TOAST_N];
static bool toastInit = false;

static int16_t toastSpawnY(int band) {            // sprite top-left y within a band
    int base = (band == 0) ? TOP_Y : BOT_Y;
    return base + 2 + random(0, 7);
}
static void drawToaster(int x, int y, float flap) {
    int fy = (int)(sinf(flap) * 6.0f);            // wing flap offset
    extDisplay.fillTriangle(x + 21, y + 9, x + 31, y + 9 + fy, x + 21, y + 16, TFT_WHITE); // wing (trailing)
    extDisplay.fillRoundRect(x + 5, y + 6, 18, 13, 3, 0xCE59);   // chrome body
    extDisplay.drawRoundRect(x + 5, y + 6, 18, 13, 3, 0x8410);   // edge
    extDisplay.fillRect(x + 8, y + 8, 12, 2, 0x4208);            // slot
    extDisplay.fillRoundRect(x + 10, y + 1, 8, 6, 1, 0xE5A0);    // toast popping up
}
static void drawToast(int x, int y) {
    extDisplay.fillRoundRect(x + 4, y + 5, 16, 12, 3, 0xD480);   // tan slice
    extDisplay.drawRoundRect(x + 4, y + 5, 16, 12, 3, 0x9320);   // crust
    extDisplay.fillRect(x + 8, y + 9, 3, 2, 0xB340);             // toasty specks
    extDisplay.fillRect(x + 13, y + 11, 2, 2, 0xB340);
}
static void drawNyan(int x, int y, int phase) {
    static const uint16_t rb[6] = { 0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x051F, 0xA81F }; // R O Y G B V
    // Rainbow trail FIRST, started under the body so it emerges from behind the cat.
    for (int seg = 0; seg < 8; seg++) {                          // stepped rainbow trail (behind)
        int tx = x + 20 + seg * 6;
        int oy = ((seg + phase) & 1) ? 2 : 0;
        for (int k = 0; k < 6; k++)
            extDisplay.fillRect(tx, y + 3 + oy + k * 3, 6, 3, rb[k]);
    }
    // ---- cat painted last, fully on top of its trail ----
    extDisplay.fillRoundRect(x + 3, y + 5, 20, 13, 3, 0xFB56);   // pop-tart body (pink)
    extDisplay.drawRoundRect(x + 3, y + 5, 20, 13, 3, 0xCAE0);   // crust
    extDisplay.drawFastHLine(x + 6, y + 8, 14, 0xFCB8);          // frosting line
    extDisplay.fillRoundRect(x - 3, y + 4, 11, 11, 3, 0x9CD3);   // gray head (front)
    extDisplay.fillTriangle(x - 2, y + 4, x,     y, x + 2, y + 4, 0x9CD3); // ear
    extDisplay.fillTriangle(x + 4, y + 4, x + 6, y, x + 8, y + 4, 0x9CD3); // ear
    extDisplay.drawPixel(x + 1, y + 8, TFT_BLACK);              // eyes
    extDisplay.drawPixel(x + 5, y + 8, TFT_BLACK);
    extDisplay.drawFastHLine(x + 1, y + 11, 4, 0xF800);        // mouth
    extDisplay.fillRect(x + 5,  y + 18, 3, 2, 0x9CD3);          // little legs
    extDisplay.fillRect(x + 13, y + 18, 3, 2, 0x9CD3);
}

#define NSTARS 14
static int16_t tstarX[NSTARS], tstarY[NSTARS];
static uint8_t tstarB[NSTARS];
static void drawTstars() {
    for (int i = 0; i < NSTARS; i++) {
        uint16_t c = tstarB[i] ? TFT_WHITE : 0x9CF3;
        extDisplay.drawPixel(tstarX[i], tstarY[i], c);
        if (tstarB[i]) extDisplay.drawPixel(tstarX[i] + 1, tstarY[i], c);
    }
}
static void toastRespawn(Flyer& f) {
    f.x = SCR_W + random(0, 40);
    f.y = toastSpawnY(random(0, 2));
    f.spd = 1 + random(0, 3);
    int roll = random(0, 22);
    f.kind = (roll == 0) ? 2 : (roll < 5) ? 1 : 0;   // rare nyan, some toast, mostly toasters
}
static void fxToasters() {
    if (!toastInit) {
        for (int i = 0; i < TOAST_N; i++) {
            Flyer& f = flyers[i];
            f.x = random(0, SCR_W); f.y = toastSpawnY(random(0, 2));
            f.spd = 1 + random(0, 3); f.kind = (random(0, 5) == 0) ? 1 : 0;
            f.flap = random(0, 100) * 0.1f; f.px = (int)f.x; f.py = f.y;
        }
        for (int i = 0; i < NSTARS; i++) {
            tstarX[i] = random(0, SCR_W);
            tstarY[i] = ((random(0, 2)) ? TOP_Y : BOT_Y) + 1 + random(0, BAND_H - 2);
            tstarB[i] = (random(0, 3) == 0) ? 1 : 0;
        }
        toastInit = true;
    }
    for (int i = 0; i < TOAST_N; i++) {           // pass 1: erase old footprints
        int band = (flyers[i].py < SCR_H / 2) ? TOP_Y : BOT_Y;
        int ew = (flyers[i].kind == 2) ? 86 : 40; // nyan trails a long rainbow
        extDisplay.fillRect(flyers[i].px - 6, band, ew, BAND_H, TFT_BLACK);
    }
    drawTstars();                                 // starry sky behind the flyers
    for (int i = 0; i < TOAST_N; i++) {           // pass 2: move + draw
        Flyer& f = flyers[i];
        f.x -= f.spd; f.flap += 0.5f;             // fly leftward, flapping
        if (f.x < -28) toastRespawn(f);
        int x = (int)f.x, y = f.y;
        if (f.kind == 2)      drawNyan(x, y, (int)f.flap);
        else if (f.kind == 1) drawToast(x, y);
        else                  drawToaster(x, y, f.flap);
        f.px = x; f.py = y;
    }
}

// ---------------- Band dispatcher ----------------
static constexpr uint32_t BAND_MS = 30;   // min ms between band redraws (~33 fps).
                                          // Lower = smoother (flicker fuses) but more
                                          // SPI/CPU load shared with SD + WiFi. Try 20
                                          // if stable; raise if SD stutters.
// ---- SQUACH-CAM: cryptid trail-cam band effect (night vision + REC + roamer) ----
//   #00FF.. night-vision wash w/ moving CRT scanlines, a blinking REC dot, a frame
//   counter, and a Squach silhouette that lopes across the lower band, then hides
//   ("caught on the trail cam"). Erase-by-rewash each frame -> no flash.
static int      scamX = -40;
static bool     scamLegPhase = false;
static bool     scamHidden = false;
static uint32_t scamHideUntil = 0;
static uint8_t  scamScan = 0;
static uint16_t scamFrame = 0;
static uint32_t scamRecMs = 0;
static bool     scamRecOn = true;

static const uint16_t SCAM_BRIGHT = 0x07E0;   // pure green
static const uint16_t SCAM_DIM    = 0x0200;   // dark green scanline
static const uint16_t SCAM_RED    = 0xF800;   // REC dot

// scanline wash for one band (y0..y0+BAND_H), full coverage -> overwrites old frame
static void scamWash(int y0) {
    for (int y = 0; y < BAND_H; y++) {
        bool lit = (((y + scamScan) % 3) == 0);
        extDisplay.drawFastHLine(0, y0 + y, SCR_W, lit ? SCAM_DIM : TFT_BLACK);
    }
    // a few drifting noise specks for vibe
    for (int i = 0; i < 4; i++)
        extDisplay.drawPixel(random(SCR_W), y0 + random(BAND_H), SCAM_BRIGHT);
}

static void scamDrawSquach(int x, bool leg, uint16_t col) {
    int by = BOT_Y;
    extDisplay.fillRect(x + 6, by + 2,  8, 8,  col);   // head
    extDisplay.fillRect(x + 4, by + 8,  12, 15, col);  // torso
    extDisplay.fillRect(x + 1, by + 9,  3, 11, col);   // arm L
    extDisplay.fillRect(x + 16, by + 9, 3, 11, col);   // arm R
    if (leg) { extDisplay.fillRect(x + 5,  by + 23, 3, 6, col);
               extDisplay.fillRect(x + 12, by + 23, 3, 4, col); }
    else     { extDisplay.fillRect(x + 5,  by + 23, 3, 4, col);
               extDisplay.fillRect(x + 12, by + 23, 3, 6, col); }
    // two eyeshine dots
    extDisplay.drawPixel(x + 8,  by + 5, 0xFFE0);
    extDisplay.drawPixel(x + 11, by + 5, 0xFFE0);
}

static void fxSquachCam() {
    uint32_t now = millis();
    scamScan = (scamScan + 1) % 3;
    scamFrame++;

    // ---- top band: night-vision wash + OSD ----
    scamWash(TOP_Y);
    extDisplay.setTextSize(1);
    // REC blink
    if (now - scamRecMs > 600) { scamRecMs = now; scamRecOn = !scamRecOn; }
    if (scamRecOn) extDisplay.fillCircle(8, TOP_Y + 8, 4, SCAM_RED);
    extDisplay.setTextColor(SCAM_BRIGHT, TFT_BLACK);
    extDisplay.setCursor(18, TOP_Y + 4);  extDisplay.print("REC  SQUACH-CAM");
    extDisplay.setCursor(SCR_W - 56, TOP_Y + 4);
    extDisplay.printf("F%05u", scamFrame);

    // ---- bottom band: night-vision wash + roaming squach ----
    scamWash(BOT_Y);
    extDisplay.setTextColor(SCAM_DIM, TFT_BLACK);
    extDisplay.setCursor(SCR_W - 70, BOT_Y + BAND_H - 9);   // 11 chars * 6px = 66px -> fits w/ margin
    extDisplay.print("NIGHTVISION");

    if (scamHidden) {
        if (now >= scamHideUntil) { scamHidden = false; scamX = -40; }
    } else {
        if ((scamFrame & 1) == 0) { scamX += 3; scamLegPhase = !scamLegPhase; }
        scamDrawSquach(scamX, scamLegPhase, SCAM_BRIGHT);
        if (scamX > SCR_W) {                 // walked off -> hide for a bit
            scamHidden = true;
            scamHideUntil = now + 2500 + random(4000);
        }
    }
}

static uint32_t fxLast = 0;
static void extDrawBands() {
    uint32_t now = millis();
    if (now - fxLast < BAND_MS) return;
    fxLast = now;
    extDisplay.startWrite();
    if (fxDirty) { bandsClear(); fxDirty = false; }
    switch (extBandEffect) {
        case FX_MATRIX:   fxMatrix();   break;
        case FX_TICKER:   fxTicker();   break;
        case FX_SPECTRUM: fxSpectrum(); break;
        case FX_FIRE:     fxFire();     break;
        case FX_STARFIELD:fxStarfield();break;
        case FX_PLASMA:   fxPlasma();   break;
        case FX_SUNSET:   fxSunset();   break;
        case FX_TOASTERS: fxToasters(); break;
        case FX_SQUACHCAM:fxSquachCam();break;   // cryptid trail-cam
        default:          /* FX_OFF: leave black */ break;
    }
    extDisplay.endWrite();
}

// ---------------- Public API ----------------
void extInit() {
    extDisplay.init();
    extDisplay.setRotation(7);
    extDisplay.setColorDepth(MATRIX_RICH ? 16 : 8);
    extDisplay.fillScreen(TFT_BLACK);
}

void extPushNative(M5Canvas& top, M5Canvas& main, M5Canvas& bottom) {
    top.pushSprite(&extDisplay, EXT_CLONE_OX, EXT_CLONE_OY);
    main.pushSprite(&extDisplay, EXT_CLONE_OX, EXT_CLONE_OY + TOP_BAR_H);
    bottom.pushSprite(&extDisplay, EXT_CLONE_OX, EXT_CLONE_OY + DISPLAY_H - BOTTOM_BAR_H);
}

// ---- Direct scale-to-fit: scale each strip straight to the panel (no buffer) ----
static bool menuBaseDrawn = false;
static void audioExitMenu();   // defined in the menu section below
static void scaleStripToPanel(M5Canvas& c, int compTopY) {
    const float z = 320.0f / (float)DISPLAY_W;                 // 240 -> 320 wide
    float compCenterY = compTopY + c.height() / 2.0f;
    float dstCenterY  = 120.0f + (compCenterY - DISPLAY_H / 2.0f) * z;  // 135 tall -> centered
    c.pushRotateZoom(&extDisplay, 160.0f, dstCenterY, 0.0f, z, z);
}
void extPushScaledFit(M5Canvas& top, M5Canvas& main, M5Canvas& bottom) {
    menuBaseDrawn = false;                                      // content overwrote the menu pig
    audioExitMenu();                                            // give I2S back to the speaker
    scaleStripToPanel(top,    0);
    scaleStripToPanel(main,   TOP_BAR_H);
    scaleStripToPanel(bottom, DISPLAY_H - BOTTOM_BAR_H);
    extDrawBands();
}

// ---------------- Menu screen: full-screen Matrix rain + expressive pig ----------------
static constexpr uint16_t PIG_PINK = 0xFDB9;
static uint32_t menuEnterMs = 0;

// ---- Audio reactive (mic only while in the menu; speaker shares the same I2S) ----
#define MIC_SAMPLES 256
static int16_t micBuf[MIC_SAMPLES];
static bool    micActive  = false;
static float   audioLevel = 0.0f;             // 0..1 smoothed loudness
static constexpr float AUDIO_GAIN = 1500.0f;  // lower = more sensitive (tune to taste)
volatile bool g_audioMicActive = false;   // true while the mic owns the shared I2S bus (SFX stays off it)

static void audioEnterMenu() {
    if (!AUDIO_REACTIVE || micActive) return;
    g_audioMicActive = true;                   // block SFX from the speaker BEFORE we take the bus
    M5.Speaker.stop();                         // silence anything mid-tone
    M5.Speaker.end();                          // free the I2S bus
    if (M5.Mic.begin()) {
        micActive = true;
        M5.Mic.record(micBuf, MIC_SAMPLES, 16000);
    } else {
        M5.Speaker.begin();                    // mic unavailable -> restore speaker, no audio fx
        g_audioMicActive = false;
    }
}
static void audioExitMenu() {
    if (!micActive) return;
    M5.Mic.end();
    M5.Speaker.begin();                        // hand the bus back to SFX
    micActive  = false;
    audioLevel = 0.0f;
    g_audioMicActive = false;                  // re-enable SFX only after the speaker is back
}
static void audioUpdate() {
    if (!micActive || M5.Mic.isRecording()) return;   // wait for the capture to finish
    int32_t mean = 0;
    for (int i = 0; i < MIC_SAMPLES; i++) mean += micBuf[i];
    mean /= MIC_SAMPLES;
    uint32_t sum = 0;
    for (int i = 0; i < MIC_SAMPLES; i++) { int32_t v = micBuf[i] - mean; sum += (v < 0 ? -v : v); }
    float lvl = ((float)sum / MIC_SAMPLES) / AUDIO_GAIN;
    if (lvl > 1.0f) lvl = 1.0f;
    audioLevel += (lvl - audioLevel) * 0.5f;          // fast attack / smooth
    M5.Mic.record(micBuf, MIC_SAMPLES, 16000);        // queue the next grab
}

// ---- Full-screen rain, drawn INCREMENTALLY (only changed cells) -> covers everything,
//      does less work than the full-cover band matrix, never flashes. Reacts to audio. ----
#define MM_COLS  53            // 320 / 6
#define MM_PITCH 8             // row height (font is 8px at size 1)
#define MM_ROWS  (SCR_H / MM_PITCH)   // 30
#define MM_TRAIL 8             // trail length in cells
static int16_t mmHead[MM_COLS];
static uint8_t mmTick[MM_COLS], mmSpd[MM_COLS];
static bool    mmInit = false;

static void mmCell(int c, int r, char ch, uint16_t col) {
    if (r < 0 || r >= MM_ROWS) return;
    extDisplay.setTextColor(col, TFT_BLACK);          // opaque -> repaints just this cell
    extDisplay.setCursor(c * 6, r * MM_PITCH);
    extDisplay.print(ch);
}
static void mmMatrix() {
    if (!mmInit) {
        for (int c = 0; c < MM_COLS; c++) {
            mmHead[c] = -(int16_t)random(0, MM_ROWS);
            mmSpd[c]  = 1 + random(0, 4);
            mmTick[c] = random(0, 4);
        }
        mmInit = true;
    }
    int dec = 1 + (int)(audioLevel * 4.0f);           // louder -> rain falls faster
    extDisplay.setTextSize(1);
    for (int c = 0; c < MM_COLS; c++) {
        if (mmTick[c] >= dec) { mmTick[c] -= dec; continue; }   // not this column's turn
        mmTick[c] = mmSpd[c];
        int h = ++mmHead[c];
        mmCell(c, h,     mbRandChar(), TFT_WHITE);     // bright head
        mmCell(c, h - 1, mbRandChar(), TFT_GREEN);     // dims to trail green
        int tr = h - MM_TRAIL;                         // erase the tail end
        if (tr >= 0 && tr < MM_ROWS) extDisplay.fillRect(c * 6, tr * MM_PITCH, 6, MM_PITCH, TFT_BLACK);
        if (tr > MM_ROWS) { mmHead[c] = -(int16_t)random(0, 14); mmSpd[c] = 1 + random(0, 4); }
    }
}

// ---- The pig: real ASCII avatar art, expressive, audio + idle aware ----
static void drawPig() {
    uint32_t t = millis();
    uint32_t idle = t - menuEnterMs;
    bool loud  = (audioLevel > 0.35f);
    bool blink = (t % 3000) < 140;
    const char* ears; const char* face;
    bool dozing = false;
    if (loud) {                                         // hears you -> excited
        ears = " !  ! "; face = "(@ 00)";
    } else if (idle > 14000) {                          // left alone -> dozes off
        ears = " v  v "; face = "(- 00)"; dozing = true;
    } else {
        switch ((t / 2600) % 4) {
            case 1:  ears = " ^  ^ "; face = "(^ 00)"; break;   // happy
            case 2:  ears = " |  | "; face = "(= 00)"; break;   // curious
            case 3:  ears = " ^  ^ "; face = "(o 00)"; break;   // content
            default: ears = " ?  ? "; face = "(o 00)"; break;   // neutral
        }
        if (blink) face = "(- 00)";
    }
    const char* body = "z(    )";
    const int S = 5, cw = 6 * S, lh = 8 * S;
    int x0 = (SCR_W - 6 * cw) / 2;
    int y0 = (SCR_H - 3 * lh) / 2;
    extDisplay.setTextSize(S);
    extDisplay.setTextDatum(TL_DATUM);
    extDisplay.setTextColor(PIG_PINK, TFT_BLACK);       // opaque box -> sits cleanly over rain
    extDisplay.setCursor(x0, y0);            extDisplay.print(ears);
    extDisplay.setCursor(x0, y0 + lh);       extDisplay.print(face);
    extDisplay.setCursor(x0 - cw, y0 + 2 * lh); extDisplay.print(body);
    if (dozing) {                                       // sleepy z's (inside box -> auto-cleared)
        const char* zzz = ((t / 500) % 3 == 0) ? "z" : ((t / 500) % 3 == 1) ? "zz" : "zzz";
        extDisplay.setTextSize(2);
        extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
        extDisplay.setCursor(x0 + 5 * cw - 6, y0);
        extDisplay.print(zzz);
    }
}

// ============ SQUACH-CAM full-screen menu backdrop + boot reveal ============
// Reuses the SCAM_* night-vision palette from the band effect above. Incremental
// (erase-by-rewash on the roamer only) so it never flashes -- same discipline as
// the matrix-pig backdrop it replaces.
static bool     scmBaseDrawn = false;
static uint32_t scmLast = 0;
static int      scmX = -60, scmPrevX = -60;
static bool     scmLeg = false, scmHidden = false;
static uint32_t scmHideUntil = 0;
static bool     scmRec = true;
static uint32_t scmRecMs = 0;
static uint16_t scmFrame = 0;

// fixed-phase night-vision scanlines over a rect (entry + erase use same phase)
static void scmField(int x, int y, int w, int h) {
    for (int yy = 0; yy < h; yy++)
        extDisplay.drawFastHLine(x, y + yy, w, ((y + yy) % 3 == 0) ? SCAM_DIM : TFT_BLACK);
}
// big squach silhouette (~60w x 110h), feet at baseY
static void scmSquach(int x, int baseY, bool leg, uint16_t col) {
    extDisplay.fillRect(x + 18, baseY - 110, 24, 26, col);   // head
    extDisplay.fillRect(x + 8,  baseY - 86,  44, 56, col);   // torso
    extDisplay.fillRect(x + 0,  baseY - 82,  10, 40, col);   // arm L
    extDisplay.fillRect(x + 50, baseY - 82,  10, 40, col);   // arm R
    if (leg) { extDisplay.fillRect(x + 12, baseY - 30, 12, 30, col);
               extDisplay.fillRect(x + 34, baseY - 30, 12, 22, col); }
    else     { extDisplay.fillRect(x + 12, baseY - 30, 12, 22, col);
               extDisplay.fillRect(x + 34, baseY - 30, 12, 30, col); }
    extDisplay.fillRect(x + 24, baseY - 100, 4, 4, 0xFFE0);  // eyeshine
    extDisplay.fillRect(x + 32, baseY - 100, 4, 4, 0xFFE0);
}
static void scmCorners() {
    uint16_t c = SCAM_BRIGHT; int L = 16;
    extDisplay.drawFastHLine(4, 4, L, c);              extDisplay.drawFastVLine(4, 4, L, c);
    extDisplay.drawFastHLine(SCR_W - 4 - L, 4, L, c);  extDisplay.drawFastVLine(SCR_W - 5, 4, L, c);
    extDisplay.drawFastHLine(4, SCR_H - 5, L, c);      extDisplay.drawFastVLine(4, SCR_H - 4 - L, L, c);
    extDisplay.drawFastHLine(SCR_W - 4 - L, SCR_H - 5, L, c); extDisplay.drawFastVLine(SCR_W - 5, SCR_H - 4 - L, L, c);
}

void extMenuSquachCam() {
    if (!scmBaseDrawn) {
        extDisplay.fillScreen(TFT_BLACK);
        scmField(0, 0, SCR_W, SCR_H);
        scmCorners();
        extDisplay.setTextSize(2);
        extDisplay.setTextColor(SCAM_BRIGHT, TFT_BLACK);
        extDisplay.setCursor(40, 12); extDisplay.print("SQUACH-CAM");
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(SCAM_DIM, TFT_BLACK);
        extDisplay.setCursor(40, 32); extDisplay.print("// CRYPTID NETWORK ONLINE //");
        scmBaseDrawn = true; scmX = scmPrevX = -60; scmHidden = false;
        menuEnterMs = millis();
    }
    uint32_t now = millis();
    if (now - scmLast < 40) return;          // ~25fps
    scmLast = now; scmFrame++;

    if (now - scmRecMs > 600) { scmRecMs = now; scmRec = !scmRec; }
    extDisplay.fillCircle(24, 19, 6, scmRec ? SCAM_RED : TFT_BLACK);
    extDisplay.setTextColor(SCAM_BRIGHT, TFT_BLACK);
    extDisplay.setCursor(SCR_W - 86, SCR_H - 16); extDisplay.printf("REC F%05u", scmFrame);

    int baseY = SCR_H - 24;
    int ex = scmPrevX - 4, ew = 72;
    if (ex < 0) { ew += ex; ex = 0; }
    if (ex + ew > SCR_W) ew = SCR_W - ex;
    if (ew > 0) scmField(ex, baseY - 116, ew, 120);     // erase old footprint

    if (scmHidden) {
        if (now >= scmHideUntil) { scmHidden = false; scmX = scmPrevX = -60; }
    } else {
        scmPrevX = scmX; scmX += 5; scmLeg = !scmLeg;
        scmSquach(scmX, baseY, scmLeg, SCAM_BRIGHT);
        if (scmX > SCR_W) { scmHidden = true; scmHideUntil = now + 2500 + random(3500); }
    }
}

void extBootIntroSquach() {
    extDisplay.fillScreen(TFT_BLACK);
    scmField(0, 0, SCR_W, SCR_H);
    scmCorners();
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(SCAM_BRIGHT, TFT_BLACK);
    extDisplay.setCursor(40, 12); extDisplay.print("SQUACH-CAM");
    extDisplay.setTextColor(SCAM_RED, TFT_BLACK);
    extDisplay.setCursor(40, 36); extDisplay.print("> REC");
    for (int x = -60; x < 130; x += 12) {                // squach lopes into frame
        scmField(x - 16, SCR_H - 140, 84, 124);
        scmSquach(x, SCR_H - 24, (x / 12) & 1, SCAM_BRIGHT);
        delay(45);
    }
    delay(350);
    extDisplay.fillScreen(TFT_WHITE); delay(40);         // power-on flash
    extDisplay.fillScreen(TFT_BLACK);
    menuBaseDrawn = false; scmBaseDrawn = false; mmInit = false;
}

static uint32_t menuLast = 0;
void extMenuPig() {
    if (!menuBaseDrawn) {
        extDisplay.fillScreen(TFT_BLACK);               // full screen is the rain now
        menuBaseDrawn = true;
        mmInit = false;                                 // restart the rain fresh
        menuEnterMs = millis();                         // reset idle timer
        audioEnterMenu();                               // mic on (if available)
    }
    uint32_t now = millis();
    if (now - menuLast < 33) return;                    // ~30 fps
    menuLast = now;
    audioUpdate();                                      // refresh loudness
    mmMatrix();                                         // rain behind (audio-reactive)
    drawPig();                                          // expressive pig on top
}

// ---- Boot intro: rain assembles, white flash, pig reveal (one-time, reuses the rain) ----
void extBootIntro() {
    extDisplay.fillScreen(TFT_BLACK);
    mmInit = false;
    uint32_t start = millis();
    while (millis() - start < 1100) { mmMatrix(); delay(28); }   // rain pours in
    extDisplay.fillScreen(TFT_WHITE); delay(40);                 // power-on flash
    extDisplay.fillScreen(TFT_BLACK);
    menuEnterMs = millis();                                      // so the pig is awake, not dozing
    drawPig();
    delay(650);
    extDisplay.fillScreen(TFT_BLACK);                            // leave clean for the first UI frame
    menuBaseDrawn = false;
    mmInit = false;
}
