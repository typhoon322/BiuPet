#include "display.h"
#include "display_config.h"

static LGFX lcd;

namespace Display {

bool init() {
    // Backlight via LEDC PWM (MusicGoGoGo pattern): 8-bit, ~5 kHz, channel 0.
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 255);

    lcd.init();
    lcd.setRotation(1);  // 240x320 -> 320x240 landscape
    lcd.fillScreen(0x0000);
    return true;
}

void clear(uint32_t color) {
    lcd.clear(color);
}

void fillScreen(uint32_t color) {
    lcd.fillScreen(color);
}

void fillRect(int x, int y, int w, int h, uint32_t color) {
    lcd.fillRect(x, y, w, h, color);
}

void setBrightness(uint8_t percent) {
    ledcWrite(0, percent);
}

int width() { return lcd.width(); }
int height() { return lcd.height(); }

}
