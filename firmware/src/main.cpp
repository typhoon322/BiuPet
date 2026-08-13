#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "config/config.h"
#include "pet/pet_state.h"
#include "pet/pet_animation.h"

Adafruit_ST7789 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
PetAnimation pet;

static const PetState DEMO_SEQUENCE[] = {
    PetState::IDLE,
    PetState::WORKING,
    PetState::WAITING,
    PetState::COMPLETED,
    PetState::ERROR,
    PetState::SLEEP,
    PetState::OFFLINE,
};
static constexpr int DEMO_COUNT = sizeof(DEMO_SEQUENCE) / sizeof(DEMO_SEQUENCE[0]);

static PetState lastShownState = static_cast<PetState>(0xFF);

void drawStatusBar(PetState state) {
    tft.fillRect(0, 0, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 7);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.print("CODEX PET");

    tft.setCursor(206, 9);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print(petStateName(state));

    tft.fillRect(0, 214, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 220);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print("phase1 demo: state cycle");
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet phase1 full-cat ===");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 255);

    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(10);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(20);

    SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
    tft.init(240, 320);
    tft.setRotation(1);
    tft.invertDisplay(false);
    tft.setSPISpeed(60000000);

    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);

    pet.begin();
    pet.setState(DEMO_SEQUENCE[0]);
    Serial.println("[PET] demo started");
}

void loop() {
    static uint32_t lastStateSwitch = millis();
    static uint32_t lastFpsLog = millis();
    static uint32_t frames = 0;
    static int demoIndex = 0;

    const uint32_t now = millis();

    if (now - lastStateSwitch >= 6000) {
        lastStateSwitch = now;
        demoIndex = (demoIndex + 1) % DEMO_COUNT;
        pet.setState(DEMO_SEQUENCE[demoIndex]);
        Serial.printf("[PET] state -> %s\n", petStateName(DEMO_SEQUENCE[demoIndex]));
    }

    pet.update(now);

    // redraw status text only when the state actually changes (avoids flicker)
    if (lastShownState != pet.state()) {
        lastShownState = pet.state();
        drawStatusBar(pet.state());
    }

    pet.draw(tft, (320 - 128) / 2, (240 - 128) / 2 + 8);
    frames++;

    if (now - lastFpsLog >= 5000) {
        const float fps = frames * 1000.0f / (now - lastFpsLog);
        Serial.printf("[PERF] fps=%.1f heap=%u\n", fps, ESP.getFreeHeap());
        lastFpsLog = now;
        frames = 0;
    }

    delay(16);
}
