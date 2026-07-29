#pragma once

// EPD_Painter panel driver — M5Paper S3 (ED047TC1, 960x540 raw-parallel EPD).
//
// Same panel class as LgfxEpdDriver's LovyanGFX path, but EPD_Painter drives the
// ESP32-S3 LCD_CAM/GDMA peripheral itself with tuned lighter/darker waveform
// pairs at three quality tiers (Task 1 vendored the library and its 859-line
// pixel-pipeline assembly). Like LgfxEpdDriver, this driver owns the panel end
// to end: usesExternalBus() == true, and the SDK's EpdBus/SPI machinery is never
// brought up for this board.
//
// EPD_Painter and LovyanGFX's Panel_EPD/Bus_EPD both program LCD_CAM + GDMA over
// the same parallel pins — they cannot both initialise. This driver is selected
// only for FREEINK_DEVICE_M5PAPERS3 (see FreeInkDisplay.cpp's selectDriver()),
// and LgfxEpdDriver is not linked with real behavior on this board (BoardConfig.h
// derives FREEINK_DRIVER_LGFX_EPD from LILYGO only).
//
// BW path only (Task 2 of the port). Grayscale is a later task.

#include "PanelDriver.h"

namespace freeink {

class EpdPainterDriver : public PanelDriver {
 public:
  EpdPainterDriver() = default;

  uint32_t spiHz() const override { return 0; }  // EPD_Painter owns LCD_CAM/GDMA directly
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveLow; }
  bool usesExternalBus() const override { return true; }
  PanelGeometry geometry() const override;

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;

 private:
  bool _ready = false;
};

// Singleton accessor (Meyers, zero-heap) — one panel, matching the other drivers.
PanelDriver& epdPainterDriver();

}  // namespace freeink
