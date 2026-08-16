# LilyGo T-Display-S3 参考笔记

官方仓库: <https://github.com/Xinyuan-LilyGO/T-Display-S3>

本文件记录移植/调试时查过的官方信息，避免反复翻文档。

## 硬件概要

- ESP32-S3 (16MB flash / 8MB PSRAM 可配), 1.9" ST7789 **320x170 横屏**
- 显示接口: **8-bit 并行 8080** (ESP32-S3 LCD_CAM 外设), 非 SPI
- 电源: USB-C + 3.7V LiPo (250mAh 小电池, 默认充电电流 **500mA**, 充电 IC = TP4065)

## 引脚表 (官方)

| 功能 | GPIO | 说明 |
|---|---|---|
| Button 1 | 0 | BOOT 按钮 (已有外部上拉) |
| Button 2 | 14 | |
| Battery Volt (ADC) | 4 | 电池电压, 1/2 分压 (R14/R12 100K/100K) |
| LCD D0-D7 | 39,40,41,42,45,46,47,48 | 并行数据线 |
| LCD WR / RD / RS(DC) | 8 / 9 / 7 | |
| LCD CS / RST | 6 / 5 | |
| LCD 背光 | 38 | PWM (LEDC) |
| LCD 电源开关 | 15 | 外设电源控制, **必须置 HIGH** |

## 关键注意事项 (官方 README 原文要点)

1. **GPIO15 = 外设电源控制引脚**, 使用前必须 `pinMode(15, OUTPUT); digitalWrite(15, HIGH);`,
   否则 LCD 等外设不工作 (电池供电时尤其明显)。充电通路也需要它。
   ```cpp
   pinMode(15, OUTPUT);
   digitalWrite(15, HIGH);
   ```
2. **电池 ADC 只能在 USB-C 未插入时读到电池电压**; 插入 USB-C 后读到的不是电池电压
   (实测: 分压采样的是 USB 与电池 OR 的电源轨, 插 USB 时约 4.8V)。
   - >4.3V → USB 在位 / 电池电压被隐藏
   - 3.0~4.2V → 电池供电, 可算电量
3. **充电状态没有 GPIO 可读**: TP4065 无 CHRG 状态输出脚, 只有红色充电 LED
   (未接电池: 闪烁/微亮; 充电中: 常亮)。固件用 "USB 在位 && 电量<100%" 推断充电。
4. SD 卡引脚 (SD CMD/CLK/D0) 仅带 SD Shield 扩展板才有, 标准版板载无 SD 卡槽。
5. 充电电流默认 500mA (issues/230); 外部 5V 供电见 issues/205。

## 固件中的对应实现

- `firmware/src/display/display_config.h` — `LGFX` 类: `Bus_Parallel8` + `Panel_ST7789`
  (panel 170x320, offset_x=35, rotation 1 → 320x170; `cfg.invert = true` 需要 INVON)
- `firmware/src/main.cpp` — `PIN_LCD_POWER_ON`(15) 拉高; 顶栏电池图标 + 电量 + 充电闪电
- `firmware/src/hardware/battery.{h,cpp}` — GPIO4 ADC 读取 + 电量曲线 + NVS 记忆
- `firmware/src/hardware/buttons.{h,cpp}` — Button1(GPIO0)/Button2(GPIO14) 短/长按

## 常用操作

- 刷固件: `pio run -e t-display-s3 -t upload --upload-port /dev/cu.usbmodemXXXX`
- 串口: T-Display-S3 无 UART 桥, 需 `-DARDUINO_USB_CDC_ON_BOOT=1` 走原生 USB CDC
- 网页配置 (AP 常开): 连热点 `CodexPet-AP` (密码 `codexpet123`) → `http://192.168.4.1`
