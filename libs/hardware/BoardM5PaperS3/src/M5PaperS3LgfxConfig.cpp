// M5Stack Paper S3 — LovyanGFX parallel-EPD board config.
//
// The M5Paper S3 carries a 960x540 16-gray ED047TC1-class raw-parallel panel, the
// same display class as the LilyGo T5 S3. Unlike the T5 S3's PCA9535 + TPS65185 rig,
// the M5Paper S3's EPD rail is a single boost enabled by the panel power pin (GPIO46);
// LovyanGFX's Bus_EPD drives that pin directly via `pinPwr`, so no external PMIC/
// expander power sequence is required here. VCOM is set in hardware on this board.
//
// Pins are the physical M5Paper S3 EPD bus, cross-validated against the community
// juicecultus/crosspoint-reader-papers3 port (lib/EPD_Painter presets) and the
// M5GFX board definition.
//
// Wired with -DFREEINK_LGFX_EPD_CONFIG=m5paperS3LgfxConfig; the FreeInk LgfxEpdDriver
// forward-declares freeink::m5paperS3LgfxConfig() and constructs its singleton from it.

#include <Arduino.h>
#include <LgfxEpdConfig.h>

namespace {

// 8-bit parallel data bus D0..D7.
constexpr int8_t EPD_D0 = 6;
constexpr int8_t EPD_D1 = 14;
constexpr int8_t EPD_D2 = 7;
constexpr int8_t EPD_D3 = 12;
constexpr int8_t EPD_D4 = 9;
constexpr int8_t EPD_D5 = 11;
constexpr int8_t EPD_D6 = 8;
constexpr int8_t EPD_D7 = 10;

// Control lines.
constexpr int8_t EPD_SPH = 13;  // source start-of-horizontal (STH)
constexpr int8_t EPD_SPV = 17;  // gate start-of-vertical (STV)
constexpr int8_t EPD_OE = 45;   // output enable
constexpr int8_t EPD_LE = 15;   // latch enable (LEH)
constexpr int8_t EPD_CL = 16;   // source clock (CKH) — routed to LCD_PCLK
constexpr int8_t EPD_CKV = 18;  // gate clock
constexpr int8_t EPD_PWR = 46;  // EPD rail enable (boost)

// i80 bus clock. 16 MHz matches the LilyGo T5 S3 (same panel) and is a safe start;
// can be raised later if refresh timing allows.
constexpr uint32_t EPD_BUS_HZ = 16'000'000;

// Extra dummy pixels per line, as used for the ED047TC1 on the LilyGo T5 S3.
constexpr uint8_t EPD_LINE_PADDING = 8;

// LovyanGFX rotation for the native landscape 960x540 scan; app-level orientation
// handles rotated reader layouts on top of this.
constexpr uint8_t EPD_ROTATION = 0;

// EPD rail power hooks. FreeInkBusEPD::powerControl() overrides LovyanGFX's base
// and calls ONLY these hooks (it never invokes Bus_EPD::powerControl), so the board
// MUST drive its own EPD power pin here — with null hooks GPIO46 is never powered and
// the panel can't refresh (it just retains the last image). The M5Paper S3 has no
// PMIC/expander sequence: GPIO46 (EPD_PWR) directly enables the panel's boost rail,
// matching the community fork's EPD_Painter power sequence.
bool epdPrepare() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, LOW);  // start with the rail off
  return true;
}
bool epdPowerOn() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delayMicroseconds(100);  // let the rail settle before the scan
  return true;
}
void epdPowerOff() { digitalWrite(EPD_PWR, LOW); }

// Tuned fast-refresh waveform for the ED047TC1 (same panel as the LilyGo T5 S3).
// LovyanGFX's default lut_fast is grainy on this panel and flashes white pixels
// black for two frames on partial updates; this single waveform drives both the
// B/W base and the anti-aliased gray overlay cleanly. Ported from
// BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp (columns 0/15 = B/W drive, 1-6/9-14 = AA
// gray nudge). Each uint32_t packs 16 2-bit phases.
#define LUT_MAKE(d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, da, db, dc, dd, de, df)                          \
  (uint32_t)((d0 << 0) | (d1 << 2) | (d2 << 4) | (d3 << 6) | (d4 << 8) | (d5 << 10) | (d6 << 12) |        \
             (d7 << 14) | (d8 << 16) | (d9 << 18) | (da << 20) | (db << 22) | (dc << 24) | (dd << 26) |   \
             (de << 28) | (df << 30))
constexpr uint32_t kFastLut[] = {
    LUT_MAKE(2, 1, 1, 1, 1, 1, 1, 3, 3, 2, 2, 2, 2, 2, 2, 1),
    LUT_MAKE(2, 3, 1, 1, 1, 1, 3, 3, 3, 3, 2, 2, 2, 2, 3, 1),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT_MAKE(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    ~0u,
    0u,
};
#undef LUT_MAKE

}  // namespace

namespace freeink {

const LgfxEpdConfig& m5paperS3LgfxConfig() {
  static const LgfxEpdConfig cfg = {
      {EPD_D0, EPD_D1, EPD_D2, EPD_D3, EPD_D4, EPD_D5, EPD_D6, EPD_D7},
      EPD_SPH,
      EPD_SPV,
      EPD_OE,
      EPD_LE,
      EPD_CL,
      EPD_CKV,
      EPD_PWR,
      EPD_BUS_HZ,
      EPD_LINE_PADDING,
      EPD_ROTATION,
      {&epdPrepare, &epdPowerOn, &epdPowerOff},  // drive GPIO46 EPD rail per refresh
      nullptr, 0,                                          // lutQuality -> LovyanGFX default
      nullptr, 0,                                          // lutText -> LovyanGFX default
      kFastLut, sizeof(kFastLut) / sizeof(kFastLut[0]),    // lutFast: tuned ED047TC1 waveform
      kFastLut, sizeof(kFastLut) / sizeof(kFastLut[0]),    // lutFastest: same
  };
  return cfg;
}

}  // namespace freeink
