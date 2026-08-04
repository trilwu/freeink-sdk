#include "EpdPainterDriver.h"

#include <BoardConfig.h>

#include <array>
#include <cstdint>
#include <cstring>

#if FREEINK_DRIVER_EPD_PAINTER
#include <EPD_Painter.h>
#include <EPD_Painter_presets.h>
#include <esp_heap_caps.h>
#endif

namespace freeink {

#if FREEINK_DRIVER_EPD_PAINTER
namespace {

// -----------------------------------------------------------------------------
// 1bpp -> 2bpp expansion table
//
// The SDK framebuffer (`fb` passed to display()) is 1 bit per pixel, MSB-first:
// bit 0x80 of byte N is the leftmost of that byte's 8 pixels, 0x01 the rightmost
// (LgfxEpdDriver.cpp:158 relies on the same `0x80 >> bit` convention). A set bit
// is white (LgfxEpdDriver::fillCanvasBW: `(b & mask) ? kGrayWhite : kGrayBlack`).
//
// EPD_Painter::paintPacked() wants 2 bits per pixel, 4 pixels per byte. Per
// EPD_Painter.S:6-12: 00 = white (unpowered), 11 = black — a set bit here is
// black, the OPPOSITE polarity of the source.
//
// The packed bit ORDER also does not mirror the source's MSB-leftmost layout.
// EPD_Painter.S:41-43 documents the packed byte explicitly: for 4 source
// pixels P0..P3 (P0 first/leftmost, P3 last/rightmost), the output byte is
// [P3P3 P2P2 P1P1 P0P0] MSB-first — i.e. the FIRST (leftmost) pixel of the
// quad lands in the LOW bit pair (bits 1:0) and the LAST (rightmost) pixel in
// the HIGH bit pair (bits 7:6). That is reversed from the source's
// MSB-leftmost convention: naively re-packing bit-for-bit would mirror every
// 4-pixel group horizontally (the failure mode the task brief calls out).
//
// One source byte (8 pixels) therefore expands to TWO destination bytes: the
// first 4 (leftmost) source pixels -> lo byte, the last 4 (rightmost) source
// pixels -> hi byte, each built with the pixel-index-reversed placement above.
struct BytePair {
  uint8_t lo;
  uint8_t hi;
};

constexpr uint8_t k2bppWhite = 0b00;
constexpr uint8_t k2bppBlack = 0b11;

constexpr uint8_t twoBppFor(uint8_t srcByte, uint8_t mask) { return (srcByte & mask) ? k2bppWhite : k2bppBlack; }

// Builds the two output bytes for one source byte's 8 pixels, applying the
// pixel-index reversal within each 4-pixel quad described above.
constexpr BytePair expandByte(uint16_t srcByte) {
  const uint8_t b = static_cast<uint8_t>(srcByte);
  // First quad (leftmost 4 source pixels: masks 0x80, 0x40, 0x20, 0x10):
  // p0 -> lo bits[1:0], p1 -> lo bits[3:2], p2 -> lo bits[5:4], p3 -> lo bits[7:6].
  const uint8_t lo = static_cast<uint8_t>(twoBppFor(b, 0x80) | (twoBppFor(b, 0x40) << 2) | (twoBppFor(b, 0x20) << 4) |
                                          (twoBppFor(b, 0x10) << 6));
  // Second quad (rightmost 4 source pixels: masks 0x08, 0x04, 0x02, 0x01):
  // p4 -> hi bits[1:0], p5 -> hi bits[3:2], p6 -> hi bits[5:4], p7 -> hi bits[7:6].
  const uint8_t hi = static_cast<uint8_t>(twoBppFor(b, 0x08) | (twoBppFor(b, 0x04) << 2) | (twoBppFor(b, 0x02) << 4) |
                                          (twoBppFor(b, 0x01) << 6));
  return {lo, hi};
}

constexpr std::array<BytePair, 256> buildExpandTable() {
  std::array<BytePair, 256> table{};
  for (uint16_t i = 0; i < 256; ++i) table[i] = expandByte(i);
  return table;
}

// Computed once at compile time (constexpr): 256 entries, no runtime cost to
// build. Each display() call then does one table lookup per source byte
// instead of LgfxEpdDriver's 8-way-branchy per-bit loop.
constexpr std::array<BytePair, 256> kExpandTable = buildExpandTable();

// Constructed from a modified copy of the preset, not the preset verbatim:
// i2c.sda/scl are forced to -1. EPD_Painter only opens the I2C bus
// (epd_painter_powerctl.cpp / EPD_Painter.cpp's Wire.begin()) to talk to a
// TPS65185/PCA9555 power chip, and this preset never sets power.tps_addr (it
// defaults to -1), so that chip is never present — pin_pwr = 46 drives power
// via direct GPIO instead. Leaving i2c.scl/sda populated makes EPD_Painter
// claim the shared bus for nothing, contending with the GT911 touch
// controller and BM8563 RTC that also live on it. A function-local static
// (Meyers singleton) avoids static-initialisation-order issues from
// depending on EPD_PAINTER_PRESET at file-scope static-init time.
EPD_Painter& painter() {
  static EPD_Painter::Config config = [] {
    EPD_Painter::Config cfg = EPD_PAINTER_PRESET;
    cfg.i2c.sda = -1;
    cfg.i2c.scl = -1;
    return cfg;
  }();
  static EPD_Painter instance(config);
  return instance;
}

// Scratch 2bpp buffer for paintPacked(); sized (width*height)/4, allocated
// once on first use. PSRAM: OPI PSRAM is enabled for this env (BOARD_HAS_PSRAM)
// and the buffer is only touched once per refresh, not from an ISR.
uint8_t* g_packed = nullptr;
uint32_t g_packedSize = 0;

// Expand the SDK's 1bpp framebuffer (`fb`, `srcBytes` long) into EPD_Painter's
// 2bpp packed format (2 * srcBytes long) using the table above. srcBytes is
// (width*height)/8 = 64800 for this panel, so this is 64800 lookups replacing
// LgfxEpdDriver's 518400-iteration per-bit unpack.
void expandToPacked(const uint8_t* fb, uint32_t srcBytes) {
  for (uint32_t i = 0; i < srcBytes; ++i) {
    const BytePair& bp = kExpandTable[fb[i]];
    g_packed[2 * i] = bp.lo;
    g_packed[2 * i + 1] = bp.hi;
  }
}

}  // namespace
#endif  // FREEINK_DRIVER_EPD_PAINTER

PanelGeometry EpdPainterDriver::geometry() const {
  const uint16_t w = BoardConfig::ACTIVE.displayWidth;
  const uint16_t h = BoardConfig::ACTIVE.displayHeight;
  const uint16_t wb = w / 8;
  return {w, h, wb, static_cast<uint32_t>(wb) * h};
}

void EpdPainterDriver::begin(EpdBus& bus) {
  (void)bus;
#if FREEINK_DRIVER_EPD_PAINTER
  const PanelGeometry geom = geometry();
  g_packedSize = (static_cast<uint32_t>(geom.width) * geom.height) / 4;
  g_packed = static_cast<uint8_t*>(heap_caps_malloc(g_packedSize, MALLOC_CAP_SPIRAM));
  if (!g_packed) {
    if (Serial)
      Serial.printf("[epd_painter] FATAL: failed to allocate %lu-byte packed buffer\n",
                    static_cast<unsigned long>(g_packedSize));
    _ready = false;
    return;
  }
  _ready = painter().begin();
  if (!_ready) {
    if (Serial) Serial.printf("[epd_painter] FATAL: EPD_Painter::begin() failed — panel not initialised\n");
  } else {
    // Boot-time sync: physically clear the sleep cover and sync EPD_Painter's
    // internal packed_screenbuffer model with the panel. clear() resets that
    // internal buffer's notion of white, then paintPacked() drives the ink
    // particles white to match. Without this, every subsequent differential
    // paint compares against a wrong reference (an uninitialised buffer vs.
    // whatever the sleep image left on the panel), which is exactly the
    // faint/ghosted text this fix addresses — see EPD_Painter.cpp:334
    // ("Zero-init so delta updates assume 'all white' as initial screen
    // state") and EPD_Painter.S's packed encoding (0b00 = white).
    memset(g_packed, 0x00, g_packedSize);  // all-white 2bpp frame: every pixel 0b00
    painter().clear();
    painter().setQuality(EPD_Painter::Quality::QUALITY_HIGH);
    painter().paintPacked(g_packed);
  }
#else
  _ready = false;
#endif
}

void EpdPainterDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)bus;
  (void)prev;
#if FREEINK_DRIVER_EPD_PAINTER
  // turnOff (the sunlight "fading fix"): LgfxEpdDriver forces an immediate
  // sleep() after the refresh. EPD_Painter has no equivalent public hook — it
  // is not exposed by the vendored library (do not modify it to add one) — so
  // this driver relies on EPD_Painter's own idle auto-power-off
  // (PanelPowerGuard, ~1s after the last paint task cycle) instead of an
  // immediate one. In practice paging is far slower than that window, so the
  // panel is unpowered between turns either way; this is the one place this
  // driver's turnOff handling is not byte-for-byte identical to
  // LgfxEpdDriver's, and it is called out here rather than silently dropped.
  (void)turnOff;
  if (!_ready || !g_packed) return;
  const PanelGeometry geom = geometry();
  const uint32_t srcBytes = geom.bufferSize;  // (width*height)/8
  expandToPacked(fb, srcBytes);

  // QUALITY_HIGH (13-step waveform) is used for every refresh mode, not just
  // Full. QUALITY_NORMAL and QUALITY_FAST use shorter waveforms that don't
  // fully drive the ink particles, which leaves visible ghost traces on this
  // panel — confirmed against the working reference implementation. This
  // makes ordinary page turns slower than QUALITY_FAST would, but unreadable
  // ghosting is the worse defect; do not "optimise" this back to a
  // mode-dependent quality without re-confirming ghosting stays fixed.
  painter().setQuality(EPD_Painter::Quality::QUALITY_HIGH);

  // Only Full means "clean the panel" — the reader forces a periodic
  // FULL_REFRESH specifically to shake off accumulated ghosting.
  // paintPacked() alone is a differential paint against EPD_Painter's
  // internal packed_screenbuffer and never clears anything, so without this,
  // ghosting would accumulate across Full refreshes.
  //
  // EPD_Painter::clear() (EPD_Painter.cpp) does two things: first a
  // differential white pass through the normal paint task (using the
  // quality just set above), which leaves packed_screenbuffer consistently
  // white; then a raw flicker/shake waveform sequence driven directly at
  // the hardware that does not touch packed_screenbuffer at all. Net
  // result: the panel is physically clean and packed_screenbuffer still
  // accurately reflects "all white", so the paintPacked() below (which
  // diffs against it) stays correct instead of comparing against a stale
  // reference.
  //
  // Half and Fast stay purely differential/quick — only their waveform
  // quality changed above, matching the reference implementation, which
  // calls clear() on Full only.
  if (mode == RefreshMode::Full) {
    painter().clear();
  }
  painter().paintPacked(g_packed);
#else
  (void)fb;
  (void)mode;
  (void)turnOff;
#endif
}

void EpdPainterDriver::deepSleep(EpdBus& bus) {
  (void)bus;
#if FREEINK_DRIVER_EPD_PAINTER
  if (_ready) {
    painter().end();
    _ready = false;
  }
#endif
}

PanelDriver& epdPainterDriver() {
  static EpdPainterDriver instance;
  return instance;
}

}  // namespace freeink
