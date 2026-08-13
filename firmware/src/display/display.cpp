#include "display.h"
#include "display_config.h"

static LGFX lcd;

namespace Display {

bool init() {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    lcd.init();
    lcd.setRotation(Config::DisplayRotation);
    lcd.setBrightness(255);
    lcd.fillScreen(0x0000);
    return true;
}

void clear(uint32_t color) {
    lcd.clear(color);
}

void fillScreen(uint32_t color) {
    lcd.fillScreen(color);
}

void setBrightness(uint8_t percent) {
    lcd.setBrightness(percent);
}

int width() { return lcd.width(); }
int height() { return lcd.height(); }

}
