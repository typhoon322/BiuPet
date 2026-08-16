#include <Arduino.h>

#include "display/display_config.h"
#include "config/config.h"
#include "pet/pet_state.h"
#include "pet/pet_animation.h"
#include "communication/ble_manager.h"
#include "communication/wifi_server.h"
#include "storage/pet_stats.h"
#include "ui/chinese_text.h"

// Screen layout (differs per board: 320x240 SPI vs 320x170 parallel 8080)
// Top bar: state name + Lv + usage + BLE/WiFi icons.
// Footer: task text (left) + whale + balance (right).
#if defined(DISPLAY_8080)
#define LAYOUT_TOP_H   18
#define LAYOUT_FOOT_Y  148
#define LAYOUT_PET_Y   18
#else
#define LAYOUT_TOP_H   20
#define LAYOUT_FOOT_Y  216
#define LAYOUT_PET_Y   66
#endif

LGFX tft;
PetAnimation pet;
BleManager ble;
WifiServer wifi;
PetStatsStore stats;

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
    stats.flush(); // persist accumulated workingSeconds on any transition
    lastShownState = static_cast<PetState>(0xFF); // refresh status/stat lines
    Serial.printf("[PET] %s -> %s\n", source, petStateName(newState));
}
static char bottomText[64] = "";
char usageText[32] = "usage: -";

// ¥ glyph: "Y" + horizontal bar (drawn manually; default font lacks ¥)
static void drawYen(LGFX& tft, int16_t x, int16_t y, uint16_t color) {
    tft.drawLine(x + 1, y + 0, x + 3, y + 3, color);
    tft.drawLine(x + 5, y + 0, x + 3, y + 3, color);
    tft.drawLine(x + 3, y + 3, x + 3, y + 7, color);
    tft.drawLine(x + 1, y + 4, x + 5, y + 4, color);
}

// Bluetooth rune (ᛒ): vertical stem + two angular bows (no text, saves space)
static void drawBleIcon(LGFX& tft, int16_t x, int16_t y, uint16_t color) {
    tft.drawLine(x + 3, y + 0, x + 3, y + 10, color);
    tft.drawLine(x + 3, y + 0, x + 7, y + 3, color);
    tft.drawLine(x + 7, y + 3, x + 3, y + 5, color);
    tft.drawLine(x + 3, y + 5, x + 7, y + 7, color);
    tft.drawLine(x + 7, y + 7, x + 3, y + 10, color);
}

// WiFi: dot + 3 arcs fanning upward
static void drawWifiIcon(LGFX& tft, int16_t x, int16_t y, uint16_t color) {
    const int16_t cx = x + 6, cy = y + 7;
    tft.fillCircle(cx, cy, 1, color);
    tft.fillArc(cx, cy, 2, 1, 225.0f, 315.0f, color);
    tft.fillArc(cx, cy, 4, 3, 225.0f, 315.0f, color);
    tft.fillArc(cx, cy, 6, 5, 225.0f, 315.0f, color);
}

void drawStatusBar(PetState state) {
    const int W = tft.width();
    const int footH = tft.height() - LAYOUT_FOOT_Y;

    // ---- 顶栏：状态名 + Lv + 用量 + BLE/WiFi 图标 ----
    tft.fillRect(0, 0, W, LAYOUT_TOP_H, ST77XX_BLACK);
    tft.setTextSize(1);

    tft.setCursor(6, 4);
    tft.setTextColor(ST77XX_CYAN);
    tft.print(petStateName(state));

    const auto& st = stats.stats();
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u", st.level);
    tft.setCursor(64, 4);
    tft.setTextColor(ST77XX_GREEN);
    tft.print(lv);

    tft.setCursor(100, 4);
    tft.setTextColor(ST77XX_MAGENTA);
    tft.print(usageText);

    const bool bleOk = ble.isOnline();
    const bool wifiOk = wifi.isConnected();
    drawBleIcon(tft, 288, 4, bleOk ? ST77XX_GREEN : ST77XX_RED);
    drawWifiIcon(tft, 302, 4, wifiOk ? ST77XX_GREEN : ST77XX_RED);

    // ---- 底栏：余额(左) + 任务描述(右) ----
    tft.fillRect(0, LAYOUT_FOOT_Y, W, footH, ST77XX_BLACK);

    // left: "DS ¥<balance>"
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(6, LAYOUT_FOOT_Y + 4);
    tft.print("DS");
    drawYen(tft, 20, LAYOUT_FOOT_Y + 3, ST77XX_WHITE);
    char bal[16];
    strncpy(bal, ble.balanceText(), sizeof(bal) - 1);
    bal[sizeof(bal) - 1] = '\0';
    tft.setCursor(29, LAYOUT_FOOT_Y + 4);
    tft.print(bal);

    // right: task text in the remaining area
    if (bottomText[0] != '\0') {
        drawChineseText(tft, 90, LAYOUT_FOOT_Y + 3, bottomText, ST77XX_CYAN, 222);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet phase2: BLE ===");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 140);  // full brightness washes black out on this panel

#if defined(DISPLAY_8080) && defined(PIN_LCD_POWER_ON)
    pinMode(PIN_LCD_POWER_ON, OUTPUT);
    digitalWrite(PIN_LCD_POWER_ON, HIGH);  // T-Display-S3 LCD power enable
#endif

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
    Serial.printf("[DISPLAY] init ok %dx%d rot=%d\n", tft.width(), tft.height(), tft.getRotation());

    stats.begin();
    pet.begin();
    pet.setState(PetState::IDLE);
    wifi.begin();
    ble.begin();
    Serial.println("[PET] ready");
}

void loop() {
    static uint32_t lastFpsLog = millis();
    static uint32_t frames = 0;
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
            snprintf(bottomText, sizeof(bottomText), "%s", wifi.pendingTask());
            lastShownState = static_cast<PetState>(0xFF);
        }
        wifi.clearPendingState();
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
        snprintf(bottomText, sizeof(bottomText), "%s", ble.taskText());
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

    // DeepSeek balance arrives over BLE (bridge fetches it every 30s)
    if (ble.balanceChanged()) {
        ble.clearBalanceChanged();
        lastShownState = static_cast<PetState>(0xFF);  // redraw status bar
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

    pet.update(now);

    if (lastShownState != pet.state() || lastShownState == static_cast<PetState>(0xFF)) {
        lastShownState = pet.state();
        drawStatusBar(pet.state());
    }

    pet.draw(tft, 0, LAYOUT_PET_Y);
    frames++;

    if (now - lastFpsLog >= 5000) {
        const float fps = frames * 1000.0f / (now - lastFpsLog);
        Serial.printf("[PERF] fps=%.1f heap=%u\n", fps, ESP.getFreeHeap());
        lastFpsLog = now;
        frames = 0;
    }

    delay(16);
}
