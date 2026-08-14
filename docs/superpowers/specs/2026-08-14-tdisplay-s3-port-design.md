# T-Display-S3 移植 —— 设计文档

日期：2026-08-14
状态：待评审

## 1. 目标

给固件加一个 **LilyGo T-Display-S3** 目标（ESP32-S3 + 1.9" ST7789 8-bit 并行 8080 屏，320×170 横屏），与现有 SPI 320×240 板并存——两块板都能编译烧录，用户烧录时指定用哪个版本。

## 2. 硬件差异

| 项 | 现有板 | T-Display-S3 |
|---|---|---|
| 接口 | SPI (4-wire) | **8-bit 并行 8080** |
| 分辨率 | 320×240 横屏 | **320×170 横屏** |
| 驱动 | ST7789 | ST7789 |
| 数据脚 | MOSI=11, SCK=12, CS=10, DC=13, RST=14, BL=3 | D0–D7=39,40,41,42,45,46,47,48；WR=8, RD=9, DC=7, CS=6, RST=5, BL=38, POWER=15 |

## 3. 方案

### 3.1 显示驱动：LovyanGFX（两种接口都支持）

- 现有板：`Bus_SPI` + `Panel_ST7789`（320×240）。
- T-Display-S3：`Bus_Parallel8` + `Panel_ST7789`（320×170）。
- 用 `#ifdef DISPLAY_8080` 切换，同一套 `LGFX` 类（LovyanGFX 是 Adafruit 兼容 API：`drawPixel/fillRect/fillCircle/setCursor/print/startWrite/setAddrWindow/writePixels` 都有）。

### 3.2 渲染栈迁移（Adafruit → LovyanGFX）

- `main.cpp`：`Adafruit_ST7789 tft` → `LGFX tft`。
- `pet_animation.cpp`：`GFXcanvas16 canvas_`（Adafruit 画布）→ `LGFX_Sprite canvas_`（LovyanGFX 画布），`draw(Adafruit_ST7789&)` → `draw(LGFX&)`。
- `chinese_text.cpp`：`U8g2_for_Adafruit_GFX`（需要 Adafruit_GFX）→ **LovyanGFX 自带 u8g2 字库支持**（`setFont(u8g2_font_wqy12_t_gb2312)` + UTF-8 `print`/`drawString`），`drawChineseText(Adafruit_GFX&)` → `drawChineseText(LGFX&)`。

### 3.3 布局适配 320×170

170px 高（比 240 少 70px），压缩为：

| 区域 | 高度 | 说明 |
|---|---|---|
| 顶栏 | 18px | 状态名 + BLE/WiFi 圆点（紧凑） |
| 猫区 | 120px | 精灵缩小（scale 1.5），脚踩在任务行上方 |
| 任务行 | 16px | 中文单行 |
| 底栏 | 16px | Lv / 用量 / 余额 单行 |

所有硬编码坐标（顶栏/任务行 y、底栏 y、`pet.draw` y 偏移、画布高度）改为用 `Config::DisplayWidth/Height` 或常量推导，避免写死 240。

### 3.4 构建

- `platformio.ini` 新增 `[env:t-display-s3]`（board 仍 esp32-s3-devkitc-1，flash/psram 配置一致，lib_deps 加 LovyanGFX）。
- 用 `-D DISPLAY_8080 -D DISPLAY_WIDTH=320 -D DISPLAY_HEIGHT=170` + 并行引脚 build_flags。
- 现有 `[env:esp32-s3-devkitc-1]` 保持不动（SPI 320×240）。

## 4. 涉及文件

| 文件 | 改动 |
|---|---|
| `firmware/platformio.ini` | 新增 `t-display-s3` env + LovyanGFX lib_dep |
| `firmware/src/display/display_config.h` | 重建：`LGFX` 类，`#ifdef` 选 SPI/并行 + 分辨率 |
| `firmware/src/main.cpp` | `tft` 类型换 LGFX；布局坐标适配 170 |
| `firmware/src/pet/pet_animation.{h,cpp}` | canvas 换 LGFX_Sprite；`draw` 签名换 LGFX；canvas 高度适配 |
| `firmware/src/ui/chinese_text.{h,cpp}` | 换 LovyanGFX 字库渲染 |
| `firmware/src/display/display.h` | 重建：Display HAL 封装（可选） |

## 5. 风险与待验证

1. **并行 8080 驱动**：`Bus_Parallel8` 的引脚配置需真机验证（我没板子）。
2. **中文渲染**：LovyanGFX 的 u8g2 中文字库 + UTF-8 渲染方式需确认（可能和 U8g2_for_Adafruit_GFX 的 API 不同）。
3. **LGFX_Sprite 与 writePixels**：画布 blit 方式需适配 LovyanGFX。
4. **布局**：320×170 下猫/文字比例真机目视校准。
5. 本移植**保证编译通过**，真机点亮/布局需用户板子到手后联调。

## 6. 验收

- [ ] `pio run -e t-display-s3` 编译通过。
- [ ] `pio run -e esp32-s3-devkitc-1` 仍编译通过（现有板不回归）。
- [ ] 用户板子到手后：点亮、中文显示、猫动画、布局目视正常。
