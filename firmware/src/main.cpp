#include <Arduino.h>
#include <WiFi.h>

#include "display/display_config.h"
#include "config/config.h"
#include "pet/pet_state.h"
#include "pet/pet_animation.h"
#include "communication/ble_manager.h"
#include "communication/wifi_server.h"
#include "hardware/battery.h"
#include "hardware/buttons.h"
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
Battery battery;
Buttons buttons;
PetStatsStore stats;

static PetState lastShownState = static_cast<PetState>(0xFF);
static bool externalControl = false;
static uint32_t showInfoUntilMs_ = 0;   // >0 => button info screen is active
static bool backlightOn_ = true;

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

// Battery: self-contained 5-level icon (no text, nothing to misalign).
// Outline + up to 5 filled slots by %, colored by level; yellow bolt while
// charging. pct < 0 means unknown (empty outline).
static void drawBatteryIcon(LGFX& tft, int16_t x, int16_t y, int pct, bool charging) {
    const int16_t bw = 17, bh = 8;
    uint16_t color = ST77XX_GREEN;
    if (pct >= 0) {
        color = (pct <= 20) ? ST77XX_RED : (pct <= 50 ? ST77XX_YELLOW : ST77XX_GREEN);
    }
    tft.drawRect(x, y, bw, bh, color);
    tft.fillRect(x + bw, y + 2, 2, 4, color);   // + terminal nub
    if (pct >= 0) {
        int lvl = (pct * 5 + 99) / 100;         // 0..5 (each 20% = one slot)
        if (lvl > 5) lvl = 5;
        for (int i = 0; i < lvl; ++i) {
            tft.fillRect(x + 1 + i * 3, y + 2, 2, bh - 4, color);   // 2px slot, 1px gap
        }
    }
    if (charging) {
        // pixel-art lightning bolt to the LEFT of the icon (6x8, complete shape)
        static const uint8_t kBolt[8] = { 0x08, 0x0C, 0x0A, 0x12, 0x11, 0x21, 0x22, 0x14 };
        const int16_t bx = x - 9;
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 6; ++c) {
                if (kBolt[r] & (0x20 >> c)) tft.drawPixel(bx + c, y + r, ST77XX_YELLOW);
            }
        }
    }
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

    // battery: self-contained icon (outline + fill + % inside) + charging bolt
    const int bpct = battery.percent();
    const bool bchg = battery.charging();
    drawBatteryIcon(tft, 236, 4, bpct, bchg);

    const bool bleOk = ble.isOnline();
    const bool wifiOk = wifi.isConnected();
    drawBleIcon(tft, 288, 4, bleOk ? ST77XX_GREEN : ST77XX_RED);
    drawWifiIcon(tft, 302, 4, wifiOk ? ST77XX_GREEN : ST77XX_RED);

    // ---- 底栏：余额(左) + 任务描述(右) ----
    tft.fillRect(0, LAYOUT_FOOT_Y, W, footH, ST77XX_BLACK);

    // left: "DS <balance>"
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(6, LAYOUT_FOOT_Y + 4);
    tft.print("DS ");
    char bal[16];
    strncpy(bal, ble.balanceText(), sizeof(bal) - 1);
    bal[sizeof(bal) - 1] = '\0';
    tft.setCursor(24, LAYOUT_FOOT_Y + 4);
    tft.print(bal);

    // right: task text in the remaining area
    if (bottomText[0] != '\0') {
        drawChineseText(tft, 90, LAYOUT_FOOT_Y + 3, bottomText, ST77XX_CYAN, 222);
    }
}

// Button 1 long press: cycle the pet animation states for a quick demo.
static void cyclePetState() {
    static const PetState order[] = {PetState::IDLE, PetState::WORKING, PetState::WAITING,
                                     PetState::COMPLETED, PetState::SLEEP};
    const PetState cur = pet.state();
    PetState next = order[0];
    for (uint8_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if (order[i] == cur) {
            next = order[(i + 1) % (sizeof(order) / sizeof(order[0]))];
            break;
        }
    }
    externalControl = true;
    applyState(next, "button");
    Serial.printf("[BTN] state -> %s\n", petStateName(next));
}

// Button 1 short press: full-screen status panel for a few seconds.
// Drawn once and only re-rendered when a displayed value changes (a per-frame
// fillScreen causes visible flicker).
static char lastInfoSig_[96] = "";

static void drawInfoScreen(uint32_t nowMs) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(1);
    int y = 8;

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, y);
    tft.print("== CodexPet ==");
    y += 18;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, y);
    tft.print("State ");
    tft.setTextColor(ST77XX_GREEN);
    tft.print(petStateName(pet.state()));
    y += 16;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, y);
    tft.print("BAT ");
    if (battery.percent() >= 0) {
        const uint16_t mv = battery.millivolts();
        tft.setTextColor(ST77XX_GREEN);
        tft.printf("%u.%02uV %d%%", mv / 1000, (mv % 1000) / 10, battery.percent());
    } else {
        tft.setTextColor(ST77XX_YELLOW);
        tft.print("--");
    }
    if (battery.charging()) { tft.setTextColor(ST77XX_YELLOW); tft.print(" [CHG]"); }
    else if (battery.usbPresent()) { tft.setTextColor(ST77XX_WHITE); tft.print(" [USB]"); }
    y += 16;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, y);
    tft.print("WiFi ");
    if (WiFi.isConnected()) {
        tft.setTextColor(ST77XX_GREEN);
        tft.print(WiFi.SSID());
        y += 16;
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(10, y);
        tft.print("IP ");
        tft.print(WiFi.localIP().toString());
    } else {
        tft.setTextColor(ST77XX_RED);
        tft.print("offline (AP ");
        tft.print(WiFi.softAPIP().toString());
        tft.print(")");
    }
    y += 16;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, y);
    tft.print("DS ");
    tft.setCursor(30, y);
    tft.print(ble.balanceText());
    y += 16;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, y);
    tft.print("Up ");
    const uint32_t up = nowMs / 1000;
    tft.print(up / 3600);
    tft.print("h ");
    tft.print((up % 3600) / 60);
    tft.print("m");
    y += 16;

    tft.setTextColor(ST77XX_MAGENTA);
    tft.setCursor(10, y);
    tft.print(usageText);
}

static void updateInfoScreen(uint32_t nowMs) {
    char sig[96];
    const String ssid = WiFi.isConnected() ? WiFi.SSID() : String("-");
    snprintf(sig, sizeof(sig), "%d|%u|%d|%d|%s|%s|%s|%lu",
             static_cast<int>(pet.state()),
             battery.millivolts() / 100,        // 0.1V resolution (hide ADC jitter)
             battery.percent(), static_cast<int>(battery.charging()),
             ssid.c_str(), ble.balanceText(), usageText, nowMs / 60000);
    if (strcmp(sig, lastInfoSig_) != 0) {
        strncpy(lastInfoSig_, sig, sizeof(lastInfoSig_) - 1);
        lastInfoSig_[sizeof(lastInfoSig_) - 1] = '\0';
        drawInfoScreen(nowMs);
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
    battery.begin();
    buttons.begin();
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
    battery.update(now);

    // --- Buttons: B1 short = info, B1 long = cycle state, B2 short = backlight ---
    switch (buttons.update(now)) {
        case PetButton::B1_SHORT:
            showInfoUntilMs_ = now + 5000;
            lastInfoSig_[0] = '\0';   // force a fresh draw
            Serial.println("[BTN] info screen");
            break;
        case PetButton::B1_LONG:
            cyclePetState();
            break;
        case PetButton::B2_SHORT:
            backlightOn_ = !backlightOn_;
            ledcWrite(0, backlightOn_ ? 140 : 0);
            Serial.printf("[BTN] backlight %s\n", backlightOn_ ? "on" : "off");
            break;
        default:
            break;
    }

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

    // battery level / charging change => redraw the status bar (the estimate
    // climbs while on USB and the bolt must appear/disappear promptly)
    {
        static int lastBatLvl = -2;
        static bool lastBatChg = false;
        const int blvl = battery.percent() >= 0 ? (battery.percent() * 5 + 99) / 100 : -1;
        const bool bchg = battery.charging();
        if (blvl != lastBatLvl || bchg != lastBatChg) {
            lastBatLvl = blvl;
            lastBatChg = bchg;
            lastShownState = static_cast<PetState>(0xFF);
        }
    }

    pet.update(now);

    // Info screen overrides the normal pet rendering while active.
    static bool infoWasActive = false;
    const bool infoActive = (now < showInfoUntilMs_);
    if (infoActive) {
        updateInfoScreen(now);
    } else {
        if (infoWasActive) {
            lastShownState = static_cast<PetState>(0xFF);   // force full redraw
        }
        if (lastShownState != pet.state() || lastShownState == static_cast<PetState>(0xFF)) {
            lastShownState = pet.state();
            drawStatusBar(pet.state());
        }
        pet.draw(tft, 0, LAYOUT_PET_Y);
    }
    infoWasActive = infoActive;
    frames++;

    if (now - lastFpsLog >= 5000) {
        const float fps = frames * 1000.0f / (now - lastFpsLog);
        Serial.printf("[PERF] fps=%.1f heap=%u\n", fps, ESP.getFreeHeap());
        lastFpsLog = now;
        frames = 0;
    }

    delay(16);
}
