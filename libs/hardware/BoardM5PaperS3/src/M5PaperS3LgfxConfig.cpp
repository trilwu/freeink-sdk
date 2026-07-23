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
      {nullptr, nullptr, nullptr},  // no external PMIC/expander: pinPwr (GPIO46) drives the rail
      // LUT fields left null -> LovyanGFX Panel_EPD default waveforms. If partial
      // updates show the full-screen black "swipe" seen on the T5 S3, port its
      // kFastLut here (see BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp).
  };
  return cfg;
}

}  // namespace freeink
