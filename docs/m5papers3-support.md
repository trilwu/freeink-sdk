# M5Stack Paper S3 (4.7" ED047TC1)

The M5Paper S3 is an ESP32-S3 board with a **960×540 16-gray ED047TC1 raw-parallel
EPD** — the same panel class as the LilyGo T5 S3 (see `lilygo-t5s3-support.md`). The
MCU clocks every row over the S3 LCD (i80) peripheral; it is a different display
class from FreeInk's SPI single-chip drivers (SSD1677/UC8253/ED2208) and reuses the
`LgfxEpdDriver`. The board differs from the T5 S3 mainly in its far simpler EPD
power path and its ADC (rather than I²C-gauge) battery read.

Reference: the community port `juicecultus/crosspoint-reader-papers3` (a hand-rolled
M5GFX/EPD_Painter port) — its pin map was cross-validated against this profile.

## Display

`LgfxEpdDriver` wraps **LovyanGFX's `Panel_EPD`/`Bus_EPD`** (bundled in
`m5stack/M5GFX`) and compiles under `FREEINK_DRIVER_LGFX_EPD` (derived from
`FREEINK_DEVICE_M5PAPERS3`), so M5GFX links only on this device. The driver holds an
8-bit grayscale `LGFX_Sprite` canvas (~518 KB) in **OPI PSRAM**.

`BoardConfig::M5PAPER_S3` carries the geometry (960×540), `DisplayController::LgfxEpd`,
the GT911 touch config, and the GPIO3 ADC battery. The parallel bus pins live in the
board-supplied `LgfxEpdConfig`:

```cpp
namespace freeink {
const LgfxEpdConfig& m5paperS3LgfxConfig() {
  static const LgfxEpdConfig cfg = {
      {6, 14, 7, 12, 9, 11, 8, 10},  // 8-bit data bus D0..D7
      /*SPH*/ 13, /*SPV*/ 17, /*OE*/ 45, /*LE*/ 15, /*CL*/ 16, /*CKV*/ 18,
      /*PWR*/ 46, /*busHz*/ 16'000'000, /*linePadding*/ 8, /*rotation*/ 0,
      {nullptr, nullptr, nullptr},   // no external PMIC/expander sequence
  };
  return cfg;
}
}
```

**Power path.** Unlike the T5 S3's PCA9535 IO-expander + TPS65185 charge pump, the
M5Paper S3's EPD rail is a single boost **enabled by the panel PWR pin (GPIO46)**,
which LovyanGFX's `Bus_EPD` drives directly via `pinPwr`. VCOM is set in hardware.
So all three `LgfxEpdPowerHooks` are null — no board power sequence is required. (If a
unit needs the AXP2101 to pre-enable an EPD DCDC rail, add that in the `prepare` hook.)

A build sets `-DFREEINK_DEVICE_M5PAPERS3=1 -DFREEINK_LGFX_EPD_CONFIG=m5paperS3LgfxConfig`
and adds `m5stack/M5GFX` to that env's `lib_deps`. The config function lives in the
board-support lib `libs/hardware/BoardM5PaperS3/`.

## Peripherals

- **Touch** — GT911 on I²C (SDA41 / SCL42 / INT48, no reset GPIO), handled by
  `InputManager`. The profile uses `BoardConfig::M5PAPER_S3_GT911`. Like the classic
  M5Paper, the GT911 boots without a reset/config dance and reports coordinates at
  byte 0 (`gt911CoordsAtByte0 = true`); the portrait 540×960 digitizer is mapped onto
  the landscape 960×540 panel with `swapXY` + `flipY`.
- **Battery** — read on the **GPIO3 ADC** with a ~2.04× hardware divider
  (`batteryAdc = 3`, `batteryDividerMultiplier = 2.04`); no I²C fuel gauge. The
  AXP2101 PMIC owns charging.
- **SD card** — SPI: SCLK39 / MISO40 / MOSI38 / CS47.
- **Power button and sleep** — the power button is wired to the **AXP2101 PMIC**, not
  a GPIO, so there is no software power-button pin (`input.power = PIN_UNASSIGNED`) and
  no `ext0/ext1` wake source. The PMIC re-powers the SoC on button press (cold boot).
  Software power-off is a **GPIO44 pulse** to the PMIC — board-support, not a
  `PowerConfig` latch (the AXP2101 self-latches, so `power = {}`).

## Board-support (outside the SDK)

- **AXP2101 PMIC / BM8563 RTC** — board peripherals the SDK does not yet cover. The
  RTC (BM8563 @ 0x51 on the touch I²C bus) and the GPIO44 power-off pulse can be added
  in the consuming firmware's board-support layer.

## Status / bring-up notes

- **VCOM / waveforms** — default LovyanGFX `Panel_EPD` waveforms are used. If partial
  updates show the full-screen black "swipe" seen on the T5 S3, port its `kFastLut`
  (see `BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp`).
- **Touch orientation** — `swapXY`/`flipY` validated against the reference port; verify
  on hardware across orientations.
