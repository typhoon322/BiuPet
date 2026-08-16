#pragma once
#include "display/display_config.h"
#include <cstdint>

// 用 LovyanGFX 内置 efont 中文字库在 (x, y) 处绘制 UTF-8 文本，按像素宽度截断。
void drawChineseText(LGFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx);
