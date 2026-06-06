# External ILI9341 — dual-screen mode

Stock M5PORKCHOP plus a dual-screen split:
- External 2.8" ILI9341 (320x240 landscape) = the main show. Action modes
  (IDLE, OINK, DNH, WARHOG, PIGGYBLUES, SPECTRUM, BACON, FILE_TRANSFER,
  PIGSYNC call, CHARGING) render full-screen, scaled-to-fit (crisp, with thin
  black letterbox bars top/bottom).
- Built-in Cardputer screen = always shows the current view natively. The menu
  and all text screens (Settings, Captures, Stats, Achievements, Diagnostics,
  BoarBros, WiGLE, Unlockables, Bounty, PigSync select, SD format, crash viewer)
  render HERE. While you're in a menu/text screen, the TFT shows the LIVE idle
  pig scene (animated) as a backdrop -- it no longer freezes. Note: it always
  shows the idle pig regardless of which mode you opened the menu from.

## Files
- NEW  src/ui/ext_display.h / .cpp  — ILI9341 driver (FSPI/SPI2, bus_shared,
  rot 7) + dual-screen push helpers + a 240x135 composite buffer for scaling.
- EDIT src/ui/display.cpp — include; extInit() in Display::init(); pushAll()
  now (a) always pushes native to the built-in screen and (b) pushes a
  scaled-to-fit frame to the TFT for action modes only.

## Before flashing
- Turn GPS OFF in Settings (no module; it would drive GPIO 3/5/6 = RST/DC/CS).

## Heap / performance notes
- The scale buffer (extFrame) is ~32KB at 8-bit. It's allocated once at boot.
  If allocation ever fails on low heap, the code falls back to a centered 1:1
  clone on the TFT automatically (no crash).
- Scaling runs every frame. On this no-PSRAM chip over a shared bus it MAY lower
  the framerate / make animation a little choppier. If it's too slow:
    * raise freq_write in ext_display.h (27 -> 40 MHz) — watch SD stability, or
    * switch action modes to extPushNative() (no scaling) in pushAll().

## What to check
1. Action modes (IDLE etc.) fill the TFT, crisp, thin black bars top/bottom.
2. Press ` (menu): menu appears on the BUILT-IN screen; TFT freezes on the last
   action frame. Settings/Captures/Stats also appear on the built-in screen.
3. SD still mounts (serial: no "Config init failed" / f_mount errors).
4. Animation smoothness — report if the pig is noticeably choppier than the clone.

## Wiring (EXT header)
  CS=GPIO5  RST=GPIO3  DC=GPIO6  MOSI=GPIO14  SCK=GPIO40  MISO=unwired  3V3/GND

## Letterbox band effects (switchable)
The two thin bands (top/bottom 30px) run one of several effects, cycled with the
LEFT (,) and RIGHT (/) keys while in an action mode (IDLE, OINK, DO-NO-HAM,
WARHOG, PIGGYBLUES, BACON). Spectrum mode is excluded because it uses those keys
to pan. Effects, in order:
  0 MATRIX    - classic green code rain, long fading trails (MB_ROWS=6, MB_PITCH=5)
  1 TICKER    - scrolling list of detected APs (SSID / channel / RSSI), live
  2 SPECTRUM  - animated channel analyzer: smoothed bars + falling peak caps, and a
               white "sweep" bar that tracks the channel the scanner is hopping to
  3 FIRE      - WiFi-reactive turbulent flames: each AP seeds a hot tongue at its
               own column (taller = stronger signal), overall blaze + sparks scale
               with live packet traffic (quiet = calm, busy = roaring). Tunables in
               fireRefresh/fireStep: rate/300 (energy), 0.45 (AP tongue height),
               floor, spark rate, 0.40 smoothing.
  4 STARFIELD - stars streaking across both bands at warp speed
  5 PLASMA    - flowing cyan<->magenta plasma field (vaporwave)
  6 SUNSET    - synthwave drive: animated sky (bobbing slitted sun + twinkling stars)
               up top, a car on a scrolling perspective road below
  7 TOASTERS  - After Dark homage: chrome toasters (trailing flapping wings) + toast
               flying left over a starry sky, with a rare Nyan Cat + rainbow trail;
               erase-old rendering so no flash
  8 OFF       - black bands

Notes:
- Live data (TICKER/SPECTRUM) comes from the background scanner, refreshed ~1/s.
  In BLE mode (PIGGYBLUES) WiFi is off, so those show empty/idle.
- Default color depth is 8-bit (fast). PLASMA and SUNSET band hard at 8-bit;
  set MATRIX_RICH=true in ext_display.cpp for smooth 16-bit gradients (~2x
  slower TFT). The other effects look fine at 8-bit.
- Selection resets to MATRIX on reboot (not saved).

## Memory: no scale buffer (direct scaling)
The 32KB 240x135 composite/scale buffer was removed to fix CRITICAL heap pressure.
Big-screen modes now scale the three Porkchop canvases (top/main/bottom) straight to
the panel via pushRotateZoom in scaleStripToPanel() (~32KB reclaimed). Possible minor
seam line where the strips meet (float rounding) - acceptable tradeoff.

## Menu screen: full-screen Matrix + expressive pig
The menu/text backdrop is now a FULL-SCREEN Matrix rain with the pig sitting in it.
The rain (mmMatrix) is drawn incrementally - per falling column it only writes the new
white head cell, dims the one above to green, and erases the tail end (~3 cells/step) -
so it covers the whole panel while doing less work than the full-cover band matrix, and
never flashes. The pig (drawPig) renders the real ASCII avatar art at text size 5 with
an opaque black background (a clean box over the rain) and cycles expressions every
~2.6s (neutral/happy/excited/sleepy/curious) plus blinks. ~30fps throttle. On menu entry
the screen is cleared and the rain restarts. Big-screen modes still use the letterbox
bands; only the menu uses the full-screen rain.

## Menu extras: boot intro, idle antics, glitch/CRT, audio-reactive
- Boot intro (extBootIntro, called once in Display::init after extInit): full-screen rain
  pours in ~1.1s, a white power-on flash, then the pig is revealed; leaves a clean screen.
- Idle antics (drawPig): the menu pig cycles expressions + blinks; after ~14s with no menu
  re-entry it dozes off (sleepy face + cycling "z/zz/zzz").
- Audio-reactive (AUDIO_REACTIVE toggle, top of ext_display.cpp): the mic is enabled ONLY
  while in the menu (audioEnterMenu) and handed back to the speaker on exit (audioExitMenu,
  also called from extPushScaledFit). Loud sound -> rain falls faster + pig goes excited.
  Mic and speaker share I2S on the Cardputer, so menu SFX are silent while the mic is live;
  all other-mode SFX are unaffected. Fully guarded: if M5.Mic.begin() fails, audio fx are
  skipped and everything else runs normally. Tune sensitivity via AUDIO_GAIN (lower = more).

## SQUACH-CAM (ported from the other fork)
Pure rendering, no radio/modes. Two parts:
- Band effect (fxSquachCam, enum FX_SQUACHCAM): a night-vision trail-cam for the letterbox
  bands - moving CRT scanlines, blinking REC dot, frame counter, and a squach silhouette
  that lopes across the bottom band then hides. In the , / band-effect cycle of action modes
  (order: ...TOASTERS, SQUACHCAM, OFF). Always available.
- Full-screen menu backdrop + boot (extMenuSquachCam / extBootIntroSquach): a full trail-cam
  menu screen + a power-on reveal where the squach lopes into frame. These are wired behind
  two independent toggles in ext_display.h: SQUACHCAM_BOOT (default true = squach trail-cam
  boot reveal) and SQUACHCAM_MENU (default false = keep the matrix/pig menu backdrop). Current
  setup: squach boot + squach band bars + pig matrix menu. Flip either toggle to change.
Tuning lives at the top of each function (roamer speed scamX/scmX, hide interval, REC blink).

## Fix: menu->main freezes (mic/speaker I2S conflict)
Cause: audio-reactive grabs the mic on menu entry (M5.Speaker.end + M5.Mic.begin), but the
SFX engine (SFX::update(), pumped every loop from display.cpp) keeps calling M5.Speaker.tone()
- e.g. the MENU_CLICK fired on every menu move/select. Calling the speaker while the mic owns
the shared I2S bus stalls the peripheral -> freeze, worst at the menu->main transition (the
select sound fires the same instant the mode changes, before the bus is handed back).
Fix: a shared flag `g_audioMicActive` (defined in ext_display.cpp, extern in sfx.cpp). It is
set true BEFORE the bus is taken (and the speaker is stopped first) and cleared only AFTER the
speaker is restored. While set, every speaker access in sfx.cpp (update/startSequence/play
priority-stop/stop/tone) is skipped. Audio-reactive still works; menu SFX are simply silent
while the mic is live (already the intended tradeoff). Fallback: set AUDIO_REACTIVE=false to
disable the mic entirely.
