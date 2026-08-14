# T-Display-S3 移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给固件加 `t-display-s3` 目标（ESP32-S3 + ST7789 8-bit 并行 8080，320×170 横屏），显示栈从 Adafruit_ST7789 迁移到 LovyanGFX；现有 SPI 320×240 板不回归。

**Architecture:** 用 LovyanGFX 统一两种接口——`Bus_SPI`（现有板）vs `Bus_Parallel8`（T-Display-S3），`#ifdef DISPLAY_8080` 切换；画布从 `GFXcanvas16` 换 `LGFX_Sprite`；中文从 `U8g2_for_Adafruit_GFX` 换 LovyanGFX 自带 u8g2 字库。

**Tech Stack:** PlatformIO + Arduino-ESP32、LovyanGFX（替换 Adafruit_ST7789/GFX 渲染层）。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `firmware/platformio.ini` | 加 `t-display-s3` env + LovyanGFX lib_dep |
| `firmware/src/display/display_config.h` | 重建 `LGFX` 类（#ifdef 选 SPI/并行 + 分辨率 + 引脚） |
| `firmware/src/main.cpp` | `tft` 类型换 LGFX；布局坐标用 `DISPLAY_WIDTH/HEIGHT` |
| `firmware/src/pet/pet_animation.{h,cpp}` | `GFXcanvas16` → `LGFX_Sprite`；`draw(LGFX&)` |
| `firmware/src/ui/chinese_text.{h,cpp}` | `U8g2_for_Adafruit_GFX` → LovyanGFX `setFont` + UTF-8 |

---

## Task 1: platformio.ini + LovyanGFX 依赖 + LGFX 类

- [ ] **Step 1:** 在 `platformio.ini` 的 `lib_deps`（common 部分）加 `lovyan03/LovyanGFX@^1.1.12`。

- [ ] **Step 2:** 新增 `[env:t-display-s3]`（复制现有 env，改 build_flags）：

```ini
[env:t-display-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
lib_deps =
    bblanchon/ArduinoJson@^7.3.0
    lovyan03/LovyanGFX@^1.1.12
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.f_flash = 80000000L
board_build.f_cpu = 240000000L
board_build.psram_type = opi
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.partitions = default_16MB.csv
build_flags =
    -D DISPLAY_8080
    -D DISPLAY_WIDTH=320
    -D DISPLAY_HEIGHT=170
    -D DISPLAY_ROTATION=1
    -D BLE_DEVICE_NAME=\"CodexPet\"
    -D BOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=0
```

（现有 `[env:esp32-s3-devkitc-1]` 的 lib_deps 也加上 LovyanGFX，保持两边一致，但其 build_flags 不动。）

- [ ] **Step 3:** 重建 `firmware/src/display/display_config.h`，写 `LGFX` 类，`#ifdef DISPLAY_8080` 分支：

```cpp
#pragma once
#include <LovyanGFX.hpp>
#include "config/config.h"

class LGFX : public lgfx::LGFX_Device {
#if defined(DISPLAY_8080)
    lgfx::Bus_Parallel8 _bus;
#else
    lgfx::Bus_SPI _bus;
#endif
    lgfx::Panel_ST7789 _panel;

public:
    LGFX(void) {
        {
            auto cfg = _bus.config();
#if defined(DISPLAY_8080)
            // T-Display-S3: 8-bit 8080 parallel (ESP32-S3 LCD_CAM peripheral)
            cfg.freq_write = 16000000;
            cfg.pin_wr = 8;    // WR
            cfg.pin_rd = 9;    // RD
            cfg.pin_rs = 7;    // DC/RS
            cfg.pin_d0 = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
            cfg.pin_d4 = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
#else
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 60000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = PIN_LCD_SCK;
            cfg.pin_mosi = PIN_LCD_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_LCD_DC;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
#endif
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_LCD_CS;
            cfg.pin_rst = PIN_LCD_RST;
            cfg.pin_busy = -1;
#if defined(DISPLAY_8080)
            cfg.memory_width = 240;   // ST7789 memory is 240x320
            cfg.memory_height = 320;
            cfg.panel_width = 170;    // visible 170x320
            cfg.panel_height = 320;
            cfg.offset_x = 35;        // center 170 in 240 memory
            cfg.offset_y = 0;
#else
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
#endif
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};
```

- [ ] **Step 4:** 在 `config/config.h` 为 8080 模式补默认引脚宏（`PIN_LCD_CS=6, RST=5`，并行数据脚不用宏、直接写在 LGFX 类里）。

- [ ] **Step 5:** 编译 `pio run -e t-display-s3` 看能否解析 LovyanGFX（此阶段还没迁渲染代码，先只验证库和 LGFX 类能编译）。

---

## Task 2: 迁移 main.cpp 渲染

- [ ] **Step 1:** `#include <Adafruit_ST7789.h>` 和 `#include <Adafruit_GFX.h>` → `#include "display/display_config.h"`；`Adafruit_ST7789 tft(...)` → `LGFX tft;`。

- [ ] **Step 2:** `setup()` 里：删掉手工 `SPI.begin`/`tft.init`/`setRotation`/`setSPISpeed` 的手动初始化，改 `tft.init(); tft.setRotation(1); tft.setBrightness(140);`（LovyanGFX 自带 SPI 初始化）。背光 PWM 用 `tft.setBrightness` 或保留 `ledcWrite`。

- [ ] **Step 3:** 布局坐标改为用 `Config::DisplayWidth/DisplayHeight`（或 `DISPLAY_WIDTH/HEIGHT` 宏）推导：顶栏 y、任务行 y、底栏 y、`pet.draw` 的 y 偏移、右侧圆点/余额的 x 坐标都用 `tft.width()/height()` 算，不再写死 320/240。

- [ ] **Step 4:** 编译 + 烧录现有板验证不回归（`pio run -e esp32-s3-devkitc-1 -t upload`）。

---

## Task 3: 迁移 pet_animation 画布

- [ ] **Step 1:** `pet_animation.h`：`#include <Adafruit_ST7789.h>` → `#include "display/display_config.h"`；`draw(Adafruit_ST7789&)` → `draw(LGFX&)`；`GFXcanvas16 canvas_{...}` → `LGFX_Sprite canvas_;`（构造时 `canvas_.setColorDepth(16)`）。

- [ ] **Step 2:** `pet_animation.cpp`：`draw(LGFX& tft, ...)`；`canvas_.getBuffer()` 的 blit 段改为 `tft.pushImage(x, y, CANVAS_W, CANVAS_H, (uint16_t*)canvas_.getBuffer())`（LovyanGFX 用 `pushImage` 替代 `setAddrWindow`+`writePixels`）；`CANVAS_H` 按 `Config::DisplayHeight` 调整。

- [ ] **Step 3:** 编译验证。

---

## Task 4: 迁移 chinese_text 中文字库

- [ ] **Step 1:** `chinese_text.cpp` 去掉 `U8g2_for_Adafruit_GFX`，改用 LovyanGFX：`g.setFont(u8g2_font_wqy12_t_gb2312)` + `g.setTextDatum`/`g.drawString`/`g.print` 渲染 UTF-8。`drawChineseText(Adafruit_GFX&)` → `drawChineseText(LGFX&)`。

- [ ] **Step 2:** 若 LovyanGFX 对 GB2312 字体不是直接 UTF-8（需确认），回退用「逐字 `unicode→GB2312` 查字模 + `drawGlyph`」的方式（复用现有 `text_convert` 的 `unicode_to_gb2312` 逻辑，但画到 LGFX）。

- [ ] **Step 3:** 编译验证。

---

## Task 5: 双 env 编译通过 + 收尾

- [ ] **Step 1:** `pio run -e esp32-s3-devkitc-1` 与 `pio run -e t-display-s3` 都 SUCCESS。

- [ ] **Step 2:** 现有板烧录回归验证（状态/中文/猫/布局正常）。

- [ ] **Step 3:** 提交；README/文档注明两种板的烧录命令。

---

## 自检 / 风险

- **并行 8080 引脚/时序**：`Bus_Parallel8` 的 `freq_write`、WR/RD/DC 引脚需真机验证（无板子）。
- **中文 UTF-8 vs GB2312**：LovyanGFX 的 u8g2 中文字库编码需 Task 4 确认。
- **`pushImage` vs `writePixels`**：LovyanGFX blit 方式需 Task 3 适配。
- **布局**：320×170 猫/文字比例真机目视校准。
- **保证**：两个 env 编译通过；真机点亮/布局待用户板子到手联调。
