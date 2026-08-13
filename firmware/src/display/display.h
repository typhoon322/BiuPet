#pragma once

#include <cstdint>

namespace Display {
    bool init();
    void clear(uint32_t color = 0);
    void setBrightness(uint8_t percent);
    void fillScreen(uint32_t color);
    int width();
    int height();
}
