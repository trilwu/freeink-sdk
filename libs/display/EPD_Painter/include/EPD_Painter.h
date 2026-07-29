#ifndef EPD_Painter_H
#define EPD_Painter_H

// FreeRTOS headers — available in both Arduino-ESP32 and pure ESP-IDF
#include <esp_intr_alloc.h>
#include <esp_private/gdma.h>
#include <hal/dma_types.h>

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// I2C — TwoWire for Arduino, ESP-IDF master API otherwise
#ifdef ARDUINO
#include <Wire.h>
#else
#include "driver/i2c_master.h"
#endif

class EPD_Painter {
 public:
  struct I2CBusConfig {
#ifdef ARDUINO
    TwoWire* wire = nullptr;
#else
    i2c_master_bus_handle_t i2c_bus = nullptr;
#endif
    int sda = -1;
    int scl = -1;
    uint32_t freq = 100000;
  };
  struct PowerCtlConfig {
    int pca_addr = -1;
    int tps_addr = -1;
  };
  enum class Quality { QUALITY_HIGH, QUALITY_NORMAL, QUALITY_FAST };

  enum class Rotation {
    ROTATION_0,  // landscape — normal orientation
    ROTATION_CW  // 90° clockwise — portrait drawing canvas (width↔height swapped)
  };

  struct Waveforms {
    uint8_t fast_lighter[3][7];
    uint8_t fast_darker[3][7];
    uint8_t normal_lighter[3][13];
    uint8_t normal_darker[3][13];
    uint8_t high_lighter[3][13];
    uint8_t high_darker[3][13];
  };

  struct Config {
    uint16_t width;
    uint16_t height;
    int8_t pin_pwr;
    int8_t pin_syspwr = -1;
    int8_t pin_sph;
    int8_t pin_oe;
    int8_t pin_cl;
    int8_t pin_spv;
    int8_t pin_ckv;
    int8_t pin_le;
    Quality quality;
    Rotation rotation = Rotation::ROTATION_0;
    int8_t data_pins[8];
    I2CBusConfig i2c{};
    PowerCtlConfig power{};
    Waveforms waveforms;

    Config withRotation(Rotation r) const {
      Config c = *this;
      c.rotation = r;
      return c;
    }
  };

  Config _config;

  EPD_Painter(const Config& config, bool portrait = false);
  bool begin();
  bool end();

  void clear();
  void fxClear();
  void paint(uint8_t* framebuffer);
  void paintPacked(const uint8_t* packed);
  void unpaintPacked(const uint8_t* packed);
  void paintLater(uint8_t* framebuffer);
  void setInterlaceMode(bool mode) { interlace_mode = mode; }

  void setQuality(Quality quality);

  static void dither(uint8_t* fb, uint16_t width, uint16_t height);

  Config getConfig() { return _config; }

 private:
  // ---- LCD_CAM / DMA ----
  gdma_channel_handle_t dma_chan = nullptr;
  int _dma_channel_id = 0;
  dma_descriptor_t dma_desc1 = {};
  dma_descriptor_t dma_desc2 = {};
  intr_handle_t _lcd_intr_handle = nullptr;
  volatile TaskHandle_t _dma_notify_task = nullptr;
  bool _dma_pending = false;

  static void IRAM_ATTR _lcd_isr(void* arg);

  // ---- Buffers ----

  uint8_t* dma_buffer = nullptr;   // Points at one of the buffers below
  uint8_t* dma_buffer1 = nullptr;  //  Row Double buffer A
  uint8_t* dma_buffer2 = nullptr;  //. Row Double buffer B

  uint8_t* packed_fastbuffer = nullptr;    // 2bpp current frame  (internal RAM)
  uint8_t* packed_screenbuffer = nullptr;  // 2bpp previous frame (PSRAM)
  uint8_t* packed_paintbuffer = nullptr;   // 2bpp previous frame (PSRAM)

  uint32_t* bitmask = nullptr;

  int packed_row_bytes = 0;
  std::atomic<int> paintStage{0};
  bool interlace_mode = false;
  bool shouldSkipRow = false;

  // ---- Internal helpers ----
  void powerOn();
  void powerOff();
  void sendRow(bool firstLine, bool lastLine = false, bool skipRow = false);

  // ---- Dual-core paint task ----
  SemaphoreHandle_t _paint_active_sem = nullptr;  // signals task to start
  SemaphoreHandle_t _paint_buffer_sem = nullptr;  // signals task has finished
  TaskHandle_t _paint_task_h = nullptr;

  static void _paint_task_entry(void* arg);
  void _paint_task_body();

  // ---- Power management ----
  class PanelPowerGuard {
   public:
    PanelPowerGuard(EPD_Painter& d) : disp(d) {
      initOnce(d);
      xSemaphoreTake(power_mtx, portMAX_DELAY);
      if (state == 0) d.powerOn();
      state = 5;
      xSemaphoreGive(power_mtx);
    }

   private:
    EPD_Painter& disp;

    static inline TaskHandle_t task = nullptr;
    static inline EPD_Painter* owner = nullptr;
    static inline SemaphoreHandle_t power_mtx = nullptr;
    static inline uint8_t state = 0;

    static void initOnce(EPD_Painter& d) {
      struct Init {
        Init(EPD_Painter& d) {
          power_mtx = xSemaphoreCreateMutex();
          owner = &d;
          xTaskCreate(taskEntry, "panel_idle_off", 2048, nullptr, 1, &task);
        }
      };
      static Init init{d};
    }

    static void taskEntry(void*) {
      for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        xSemaphoreTake(power_mtx, portMAX_DELAY);
        if (state > 0) {
          state--;
          if (state == 0) owner->powerOff();
        }
        xSemaphoreGive(power_mtx);
      }
    }
  };
};

#endif
