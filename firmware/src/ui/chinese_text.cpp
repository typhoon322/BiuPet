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
    u8g2f.setForegroundColor(color);
    u8g2f.setBackgroundColor(0x0000);
    // The wqy12 _gb2312 font is keyed by Unicode (the suffix names its charset
    // coverage), so drawUTF8 is the correct API: it decodes UTF-8 to code points
    // and looks each up in the font's unicode glyph table.
    char buf[96];
    size_t n = utf8::truncate_by_width(utf8, maxPx, 6, 12);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (size_t k = 0; k < n; ++k) buf[k] = utf8[k];
    buf[n] = '\0';
    u8g2f.drawUTF8(x, y + 12, buf);
}
