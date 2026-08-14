#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>

// 用 wqy12 中文字库在 (x, y) 处绘制 UTF-8 文本，按像素宽度截断。
void drawChineseText(Adafruit_GFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx);
