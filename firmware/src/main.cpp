#include <Arduino.h>
#include "display/display.h"

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet starting ===");

    if (!Display::init()) {
        Serial.println("[ERROR] Display init failed");
        return;
    }
    Serial.printf("[INFO] Display initialized: %dx%d\n", Display::width(), Display::height());
    Display::fillScreen(0x001F); // blue screen as a quick visual smoke test
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        Serial.printf("[INFO] alive, heap=%u\n", ESP.getFreeHeap());
    }
    delay(100);
}
