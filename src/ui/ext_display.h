#ifndef PORK_EXT_DISPLAY_H
#define PORK_EXT_DISPLAY_H

// External 2.8" ILI9341 (240x320) display for M5PORKCHOP on Cardputer-ADV.
// Dual-screen mode: action modes render full-screen (fit) on the TFT; the
// built-in screen always shows the current view natively (incl. the menu).
//
// Proven config: rotation 7 (landscape 320x240), rgb_order=false.
// Shares the SD card's SPI bus -> FSPI/SPI2_HOST, bus_shared=true.
//
// Wiring (EXT header): CS=GPIO5 RST=GPIO3 DC=GPIO6 MOSI=GPIO14 SCK=GPIO40

#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_LCD.hpp>

// Native-clone placement (used only as a heap fallback if scaling can't alloc).
static constexpr int EXT_CLONE_OX = 40;   // (320 - 240) / 2
static constexpr int EXT_CLONE_OY = 52;   // (240 - 135) / 2

// ---- Local ILI9341 panel (M5GFX has no Panel_ILI9341; subclass Panel_LCD) ----
struct Panel_ILI9341_Pork : public lgfx::v1::Panel_LCD {
    Panel_ILI9341_Pork(void) {
        _cfg.memory_width  = _cfg.panel_width  = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }
protected:
    const uint8_t* getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0xEF, 3, 0x03, 0x80, 0x02,
            0xCF, 3, 0x00, 0xC1, 0x30,
            0xED, 4, 0x64, 0x03, 0x12, 0x81,
            0xE8, 3, 0x85, 0x00, 0x78,
            0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
            0xF7, 1, 0x20,
            0xEA, 2, 0x00, 0x00,
            0xC0, 1, 0x23,
            0xC1, 1, 0x10,
            0xC5, 2, 0x3E, 0x28,
            0xC7, 1, 0x86,
            0xB1, 2, 0x00, 0x18,
            0xB6, 3, 0x08, 0x82, 0x27,
            0xF2, 1, 0x00,
            0x26, 1, 0x01,
            0xE0, 15, 0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00,
            0xE1, 15, 0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F,
            CMD_SLPOUT, CMD_INIT_DELAY, 120,
            CMD_DISPON, CMD_INIT_DELAY, 120,
            0xFF, 0xFF
        };
        return (listno == 0) ? list0 : nullptr;
    }
};

class LGFX_ExtILI9341 : public lgfx::v1::LGFX_Device {
    Panel_ILI9341_Pork _panel_instance;
    lgfx::v1::Bus_SPI   _bus_instance;
public:
    LGFX_ExtILI9341() {
        {   auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI2_HOST;     // FSPI — SAME host as the SD card
            cfg.spi_mode    = 0;
            cfg.freq_write  = 27000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = 0;
            cfg.pin_sclk    = 40;
            cfg.pin_mosi    = 14;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = 6;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {   auto cfg = _panel_instance.config();
            cfg.pin_cs           = 5;
            cfg.pin_rst          = 3;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;     // shared with SD card
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

extern LGFX_ExtILI9341 extDisplay;

// Band effect selector (cycled by left/right keys in action modes)
extern uint8_t extBandEffect;
void extCycleBandEffect(int dir);

void extInit();   // call once inside Display::init()

// Push the three Porkchop canvases to the external TFT.
//   ScaledFit : composite + scale-to-fit (320x180, letterboxed). For action modes.
//   Native    : centered 1:1 (heap fallback only).
void extPushScaledFit(M5Canvas& top, M5Canvas& main, M5Canvas& bottom);
void extPushNative(M5Canvas& top, M5Canvas& main, M5Canvas& bottom);

// Low-level access for a custom-composited frame (e.g. the menu backdrop):
void extMenuPig();            // big animated pig shown on the TFT during menus/text screens
void extBootIntro();          // one-time matrix->pig reveal animation, call once at boot

#endif // PORK_EXT_DISPLAY_H
