#include "chinese_text.h"
#include "text_convert.h"

void drawChineseText(LGFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx) {
    // LovyanGFX bundles Unicode-keyed CJK efont fonts (efontCN_12 = 12px).
    // print/drawString decode UTF-8 natively (TextStyle.utf8 defaults true).
    g.setFont(&fonts::efontCN_12);
    // single-arg setTextColor leaves fore==back, so U8g2font draws no background fill.
    // (TextStyle.datum defaults to top_left, matching the old drawUTF8 anchor.)
    g.setTextColor(color);

    char buf[96];
    size_t n = utf8::truncate_by_width(utf8, maxPx, 6, 12);   // 6px ASCII / 12px CJK
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (size_t k = 0; k < n; ++k) buf[k] = utf8[k];
    buf[n] = '\0';
    g.drawString(buf, x, y);
}
