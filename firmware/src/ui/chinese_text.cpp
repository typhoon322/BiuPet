#include "chinese_text.h"
#include <U8g2_for_Adafruit_GFX.h>
#include "text_convert.h"

namespace {
U8G2_FOR_ADAFRUIT_GFX u8g2f;
bool initialized = false;
}

void drawChineseText(Adafruit_GFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx) {
    if (!initialized) {
        u8g2f.begin(g);
        u8g2f.setFont(u8g2_font_wqy12_t_gb2312);
        initialized = true;
    }
    // ASCII ~6px，CJK ~12px
    char buf[96];
    utf8_to_gb2312(utf8, buf, sizeof(buf), maxPx, 6, 12);
    u8g2f.setForegroundColor(color);
    u8g2f.setBackgroundColor(0x0000);
    u8g2f.setCursor(x, y + 12);
    u8g2f.print(buf);
}
