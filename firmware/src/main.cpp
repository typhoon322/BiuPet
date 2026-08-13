#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <LittleFS.h>

#include "config/config.h"
#include "pet/pet_state.h"
#include "pet/pet_animation.h"
#include "communication/ble_manager.h"
#include "communication/wifi_server.h"
#include "storage/pet_stats.h"

Adafruit_ST7789 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
PetAnimation pet;
BleManager ble;
WifiServer wifi;
PetStatsStore stats;

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
static bool externalControl = false;

static void applyState(PetState newState, const char* source) {
    const PetState prev = pet.state();
    if (prev == newState) {
        return;
    }
    pet.setState(newState);
    if (newState == PetState::COMPLETED) {
        stats.onTaskCompleted();
    } else if (newState == PetState::ERROR) {
        stats.onError();
    }
    lastShownState = static_cast<PetState>(0xFF); // refresh status/stat lines
    Serial.printf("[PET] %s -> %s\n", source, petStateName(newState));
}
static char bottomText[64] = "demo: state cycle";
static char usageText[32] = "usage: -";

void drawStatusBar(PetState state) {
    tft.fillRect(0, 0, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 7);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.print("CODEX PET");

    const bool online = ble.isOnline() || wifi.isConnected();
    const char* onlineLabel = online ? "ONLINE" : "OFFLINE";
    tft.setCursor(196, 9);
    tft.setTextColor(online ? ST77XX_GREEN : ST77XX_RED);
    tft.setTextSize(1);
    tft.print(onlineLabel);

    tft.setCursor(252, 9);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(petStateName(state));

    tft.fillRect(0, 214, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 220);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print(bottomText);

    const auto& st = stats.stats();
    char lvLine[48];
    snprintf(lvLine, sizeof(lvLine), "Lv.%u exp %u/%u tasks:%u",
             st.level, stats.expInLevel(), PetStatsStore::EXP_PER_LEVEL, st.tasksCompleted);
    tft.fillRect(0, 28, 320, 10, ST77XX_BLACK);
    tft.setCursor(16, 29);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.print(lvLine);

    tft.fillRect(0, 38, 320, 10, ST77XX_BLACK);
    tft.setCursor(16, 39);
    tft.setTextColor(ST77XX_MAGENTA);
    tft.setTextSize(1);
    tft.print(usageText);
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet phase2: BLE ===");

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

    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
    } else {
        Serial.printf("[FS] LittleFS ready used=%u total=%u\n",
                      (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    }
    stats.begin();
    pet.begin();
    pet.setState(PetState::OFFLINE);
    ble.begin();
    wifi.begin();
    Serial.println("[PET] ready");
}

void loop() {
    static uint32_t lastStateSwitch = millis();
    static uint32_t lastFpsLog = millis();
    static uint32_t frames = 0;
    static int demoIndex = 0;
    static bool lastOnline = false;

    const uint32_t now = millis();
    ble.update(now);
    wifi.update();

    // WiFi-driven state (lower priority than BLE packets)
    if (wifi.hasPendingState()) {
        const uint8_t st = wifi.pendingState();
        if (st <= static_cast<uint8_t>(PetState::SLEEP)) {
            externalControl = true;
            applyState(static_cast<PetState>(st), "wifi");
        }
        if (strlen(wifi.pendingTask()) > 0) {
            snprintf(bottomText, sizeof(bottomText), "task: %s", wifi.pendingTask());
            lastShownState = static_cast<PetState>(0xFF);
        }
        wifi.clearPendingState();
    }
    if (wifi.hasPendingSkin()) {
        pet.refreshExternalFrames();
        wifi.clearPendingSkin();
    }

    // working-time accumulation
    static uint32_t lastWorkTick = now;
    if (pet.state() == PetState::WORKING && now - lastWorkTick >= 1000) {
        stats.addWorkingSeconds((now - lastWorkTick) / 1000);
        lastWorkTick = now;
    } else if (now - lastWorkTick >= 1000) {
        lastWorkTick = now;
    }

    // BLE-driven state
    if (ble.hasNewPacket()) {
        PetPacket pkt = ble.takePacket();
        externalControl = true;
        applyState(pkt.state, "ble");
    }

    if (ble.taskChanged()) {
        ble.clearTaskChanged();
        snprintf(bottomText, sizeof(bottomText), "task: %s", ble.taskText());
        lastShownState = static_cast<PetState>(0xFF); // force bar redraw
    }

    if (ble.usageChanged()) {
        ble.clearUsageChanged();
        const uint32_t t = ble.usageTokens();
        if (t >= 1000000) {
            snprintf(usageText, sizeof(usageText), "today: %.2fM tok", t / 1000000.0f);
        } else if (t >= 1000) {
            snprintf(usageText, sizeof(usageText), "today: %.1fk tok", t / 1000.0f);
        } else {
            snprintf(usageText, sizeof(usageText), "today: %u tok", t);
        }
        lastShownState = static_cast<PetState>(0xFF);
    }

    // offline / online transitions
    const bool online = ble.isOnline() || wifi.isConnected();
    if (online != lastOnline) {
        lastOnline = online;
        if (!online && externalControl) {
            applyState(PetState::OFFLINE, "link");
        }
        lastShownState = static_cast<PetState>(0xFF);
    }

    // demo fallback while no BLE control yet
    if (!externalControl) {
        if (now - lastStateSwitch >= 6000) {
            lastStateSwitch = now;
            demoIndex = (demoIndex + 1) % DEMO_COUNT;
            applyState(DEMO_SEQUENCE[demoIndex], "demo");
        }
    }

    pet.update(now);

    if (lastShownState != pet.state() || lastShownState == static_cast<PetState>(0xFF)) {
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
