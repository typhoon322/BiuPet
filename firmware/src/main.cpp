#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

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

// Left session-status panel on the main page (T-Display-S3 only): the pet
// animation area shifts right by this much. 0 on the full-width SPI boards.
#if defined(DISPLAY_8080)
#define AGENT_PANEL_W  100
#else
#define AGENT_PANEL_W  0
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

// Pages shown on the display: main pet view / info panel / clock / agents.
enum class Page { MAIN, INFO, CLOCK, AGENTS };
static Page page_ = Page::MAIN;

// Clock-page state kept at file scope so switching pages can force a clean
// full redraw (otherwise the previous page's pixels linger underneath).
static char clockDateSig_[32] = "";
static int clockLastHour_ = -1, clockLastMin_ = -1, clockLastSec_ = -1;

// Backlight state: 5 brightness steps (15/40/50/75/95%), toggled on/off by
// Button2 short and stepped by Button2 long. Default = level 2 (40%).
static bool backlightOn_ = true;
static int backlightLevel_ = 2;               // 1..5, default 40%
static const uint8_t  kBrightnessPct[5] = { 15, 40, 50, 75, 95 };
static const uint16_t kBrightnessLevels[5] = { 38, 102, 127, 191, 242 };   // pct*255/100
static uint32_t brightnessShowUntil_ = 0;     // >now => show the level overlay

// Main page: left agent panel needs a repaint when the list changed or the
// region was covered (overlay / page re-entry).
static bool agentPanelDirty_ = true;

// Brightness-level overlay: a centered pill with "亮度" + 5 segments + the %
// text. While shown, the page underneath is frozen so nothing flickers.
static void drawBrightnessOverlay() {
    tft.fillRoundRect(60, 70, 200, 30, 8, 0x0820);
    tft.drawRoundRect(60, 70, 200, 30, 8, 0x2146);
    drawChineseText(tft, 70, 77, "亮度", ST77XX_WHITE, 30);   // 24px wide
    for (int i = 0; i < 5; ++i) {
        const uint16_t c = (i < backlightLevel_) ? ST77XX_CYAN : 0x2108;
        tft.fillRect(104 + i * 20, 78, 14, 14, c);
    }
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", kBrightnessPct[backlightLevel_ - 1]);
    tft.setFont(&fonts::Font0);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(210, 82);
    tft.print(pct);
}

// Draw one clock digit in a fixed-width cell: erase the whole cell first (so
// a narrower digit never leaves residue) then print CENTERED in the cell, so a
// narrow '1' sits evenly instead of hugging the left edge.
static void drawClockDigit(char c, int cx, int cy, int cellW, int cellH, float size) {
    tft.fillRect(cx, cy, cellW, cellH, ST77XX_BLACK);
    tft.setFont(&fonts::Orbitron_Light_32);
    tft.setTextSize(size);
    tft.setTextColor(ST77XX_WHITE);
    char s[2] = {c, '\0'};
    const int gw = tft.textWidth(s);
    tft.setCursor(cx + (cellW - gw) / 2, cy);
    tft.print(c);
}

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
static char lastAgentsSig_[96] = "";

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
    // refresh time (wall-clock, to the second) when the bridge sent it
    if (ble.balanceTime()[0] != '\0') {
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(64, y);
        tft.print("@");
        tft.setCursor(76, y);
        tft.print(ble.balanceTime());
        tft.setTextColor(ST77XX_WHITE);
    }
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
    snprintf(sig, sizeof(sig), "%d|%u|%d|%d|%s|%s|%s|%s|%lu",
             static_cast<int>(pet.state()),
             battery.millivolts() / 100,        // 0.1V resolution (hide ADC jitter)
             battery.percent(), static_cast<int>(battery.charging()),
             ssid.c_str(), ble.balanceText(), ble.balanceTime(), usageText, nowMs / 60000);
    if (strcmp(sig, lastInfoSig_) != 0) {
        strncpy(lastInfoSig_, sig, sizeof(lastInfoSig_) - 1);
        lastInfoSig_[sizeof(lastInfoSig_) - 1] = '\0';
        drawInfoScreen(nowMs);
    }
}

// Session status colors, following the DSH web UI: idle & completed = green,
// working = blue, waiting (needs approval) = orange, error = red, offline = gray.
static const char* const kAgentStateCn[] = {"空闲", "工作中", "等待中", "已完成", "出错", "睡眠", "离线"};
static const uint16_t kAgentStateColor[] = {
    ST77XX_GREEN,    // IDLE
    0x54BF,          // WORKING (bright blue)
    0xFD20,          // WAITING (orange, needs approval)
    ST77XX_GREEN,    // COMPLETED (same green as idle)
    ST77XX_RED,      // ERROR
    0x7BEF,          // SLEEP (gray)
    0x4208,          // OFFLINE (dark gray)
};

// Agents page: list every active agent (from the bridge "AGENTS" message) with
// its state. Redraws only when the list changes.
static void drawAgentsPage() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setFont(&fonts::Font0);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 8);
    tft.print("== Agents ==");
    const uint8_t n = ble.agentCount();
    if (n == 0) {
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(10, 44);
        tft.print("no active agents");
        return;
    }
    for (uint8_t i = 0; i < n && i < 8; ++i) {
        const BleManager::AgentInfo& a = ble.agents()[i];
        const uint8_t st = (a.state <= 6) ? a.state : 0;
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(20, 34 + i * 18);
        tft.print(a.name);
        drawChineseText(tft, 170, 32 + i * 18, kAgentStateCn[st], kAgentStateColor[st], 90);
    }
}

// Main-page session list: compact left panel, session (workspace) names
// colored by status (green idle/completed, blue working, orange needs
// approval, ...). Redrawn only when the list changes or the panel region was
// covered by an overlay.
static void drawAgentPanel() {
#if defined(DISPLAY_8080)
    const int y0 = LAYOUT_TOP_H;
    const int h = LAYOUT_FOOT_Y - LAYOUT_TOP_H;
    tft.fillRect(0, y0, AGENT_PANEL_W, h, ST77XX_BLACK);
    tft.setFont(&fonts::Font0);
    tft.setTextSize(1);
    const uint8_t n = ble.agentCount();
    if (n == 0) {
        tft.setTextColor(0x4208);
        tft.setCursor(6, y0 + 6);
        tft.print("no agents");
        return;
    }
    const uint8_t rows = (n < 8) ? n : 8;
    for (uint8_t i = 0; i < rows; ++i) {
        const BleManager::AgentInfo& a = ble.agents()[i];
        const uint8_t st = (a.state <= 6) ? a.state : 0;
        tft.setTextColor(kAgentStateColor[st]);
        tft.setCursor(6, y0 + 2 + i * 15);
        const int maxCh = (AGENT_PANEL_W - 10) / 6;   // 6px per char
        char name[25];
        strncpy(name, a.name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        if (strlen(name) > static_cast<size_t>(maxCh)) name[maxCh] = '\0';
        tft.print(name);
    }
#else
    (void)0;   // no panel on the full-width SPI boards
#endif
}

static void updateAgentsPage() {
    char sig[96] = "";
    const uint8_t n = ble.agentCount();
    snprintf(sig, sizeof(sig), "%u", n);
    for (uint8_t i = 0; i < n && i < 8; ++i) {
        char part[24];
        snprintf(part, sizeof(part), "|%s:%u", ble.agents()[i].name, ble.agents()[i].state);
        strncat(sig, part, sizeof(sig) - strlen(sig) - 1);
    }
    if (strcmp(sig, lastAgentsSig_) != 0) {
        strncpy(lastAgentsSig_, sig, sizeof(lastAgentsSig_) - 1);
        lastAgentsSig_[sizeof(lastAgentsSig_) - 1] = '\0';
        drawAgentsPage();
    }
}

// Desk-clock page: NTP time, big 7-segment digits with blinking colon, Chinese
// weekday + date composed in the center. The time is printed with a black
// background each second so the old digits are erased in place (no flash);
// ':' and ' ' share the same 12px width in Font7, so the blink never shifts.
static void drawClockPage(uint32_t nowMs) {
    static const char* const kWeekCn[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

    struct tm t;
    if (!getLocalTime(&t, 10) || t.tm_year < 120) {
        if (strcmp(clockDateSig_, "SYNC") != 0) {
            strcpy(clockDateSig_, "SYNC");
            clockLastHour_ = clockLastMin_ = clockLastSec_ = -1;
            tft.fillScreen(ST77XX_BLACK);
            tft.setFont(&fonts::Font0);
            tft.setTextSize(1);
            tft.setTextColor(ST77XX_YELLOW);
            tft.setCursor(10, 60);
            tft.print("NTP syncing...");
            tft.setCursor(10, 76);
            tft.print("(needs WiFi)");
        }
        return;
    }
    if (t.tm_sec != clockLastSec_) {
        char dateSig[32];
        snprintf(dateSig, sizeof(dateSig), "%04d-%02d-%02d|%s",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, kWeekCn[t.tm_wday]);
        if (strcmp(dateSig, clockDateSig_) != 0) {
            const bool first = (clockDateSig_[0] == '\0');
            const bool fromSync = (strcmp(clockDateSig_, "SYNC") == 0);
            strcpy(clockDateSig_, dateSig);
            if (first || fromSync) tft.fillScreen(ST77XX_BLACK);
            // subtle frame
            tft.drawRoundRect(6, 6, 308, 158, 10, 0x2146);
            // date + Chinese weekday on one line at the bottom, white
            char dateLine[32];
            snprintf(dateLine, sizeof(dateLine), "%04d-%02d-%02d %s",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, kWeekCn[t.tm_wday]);
            drawChineseText(tft, 115, 142, dateLine, ST77XX_WHITE, 300);   // ~90px wide, centered
        }
        // seconds progress bar at the top (full width, 1px), fills over the minute
        tft.fillRect(16, 10, 288, 1, ST77XX_BLACK);
        const int fill = 288 * t.tm_sec / 59;
        if (fill > 0) tft.fillRect(16, 10, fill, 1, ST77XX_CYAN);

        // Fixed-width digit layout (each digit has its own cell, so the clock
        // never jumps when a digit changes):  HH:MM big, no colon - a plain
        // gap separates the hours and minutes.
        tft.setFont(&fonts::Orbitron_Light_32);
        tft.setTextSize(2.0f);
        const int dw = tft.textWidth("8");          // widest digit = cell width
        const int gap = 20;                          // HH / MM gap (no colon)
        const int x0 = (320 - (dw * 4 + gap)) / 2;   // center the HH:MM block
        const int yBig = 42, bigH = 72;
        const int xH0 = x0, xH1 = x0 + dw;
        const int xM0 = x0 + dw * 2 + gap, xM1 = x0 + dw * 3 + gap;

        if (t.tm_hour != clockLastHour_) {
            clockLastHour_ = t.tm_hour;
            drawClockDigit('0' + t.tm_hour / 10, xH0, yBig, dw, bigH, 2.0f);
            drawClockDigit('0' + t.tm_hour % 10, xH1, yBig, dw, bigH, 2.0f);
        }
        if (t.tm_min != clockLastMin_) {
            clockLastMin_ = t.tm_min;
            drawClockDigit('0' + t.tm_min / 10, xM0, yBig, dw, bigH, 2.0f);
            drawClockDigit('0' + t.tm_min % 10, xM1, yBig, dw, bigH, 2.0f);
        }
        tft.setTextSize(1.0f);
        tft.setFont(&fonts::Font0);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet phase2: BLE ===");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, kBrightnessLevels[backlightLevel_ - 1]);   // default 40%

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

    // NTP time for the clock page (UTC+8, no DST). SNTP syncs once WiFi is up
    // and keeps the internal clock ticking even if the link drops.
    configTime(8 * 3600, 0, "pool.ntp.org", "ntp.aliyun.com");
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
            // cycle pages: main -> info -> clock -> agents -> main
            page_ = static_cast<Page>((static_cast<int>(page_) + 1) % 4);
            lastShownState = static_cast<PetState>(0xFF);   // force redraw
            if (page_ == Page::INFO) lastInfoSig_[0] = '\0';
            Serial.printf("[BTN] page %d\n", static_cast<int>(page_));
            break;
        case PetButton::B1_LONG:
            cyclePetState();
            break;
        case PetButton::B1_VERY_LONG:
            // Power off: light sleep (keeps USB alive, no reset; ~1mA draw).
            // Drop GPIO15 (PWR_EN) first so the LCD AND the green power LED go
            // dark; wake = press Button 1 or 2.
            {
                tft.fillScreen(ST77XX_BLACK);
                drawChineseText(tft, 10, 56, "正在关机...", ST77XX_YELLOW, 200);
                tft.setFont(&fonts::Font0);
                tft.setTextSize(1);
                tft.setTextColor(ST77XX_WHITE);
                tft.setCursor(10, 78);
                tft.print("release to power off");
                tft.setCursor(10, 92);
                tft.print("press a button to wake");
                const uint32_t t0 = millis();
                while (digitalRead(0) == LOW && millis() - t0 < 10000) delay(20);
                ledcWrite(0, 0);   // backlight off
#if defined(PIN_LCD_POWER_ON)
                digitalWrite(PIN_LCD_POWER_ON, LOW);   // cut LCD power + green LED
#endif
                gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
                gpio_wakeup_enable(GPIO_NUM_14, GPIO_INTR_LOW_LEVEL);
                esp_sleep_enable_gpio_wakeup();
                Serial.println("[PET] light sleep");
                esp_light_sleep_start();   // returns when a button is pressed
                Serial.println("[PET] woken");
#if defined(PIN_LCD_POWER_ON)
                digitalWrite(PIN_LCD_POWER_ON, HIGH);   // re-power the LCD rail
                delay(100);
                tft.init();               // ST7789 was power-cycled: re-init
                tft.setRotation(1);
#endif
                ledcWrite(0, kBrightnessLevels[backlightLevel_ - 1]);
                backlightOn_ = true;
                lastShownState = static_cast<PetState>(0xFF);
                buttons.reinit();   // ignore the wake button's press
            }
            break;
        case PetButton::B2_SHORT:
            backlightOn_ = !backlightOn_;
            ledcWrite(0, backlightOn_ ? kBrightnessLevels[backlightLevel_ - 1] : 0);
            Serial.printf("[BTN] backlight %s\n", backlightOn_ ? "on" : "off");
            break;
        case PetButton::B2_LONG:
            // 5-step brightness; show the level for 2s
            backlightOn_ = true;
            backlightLevel_ = (backlightLevel_ % 5) + 1;
            ledcWrite(0, kBrightnessLevels[backlightLevel_ - 1]);
            brightnessShowUntil_ = now + 2000;
            Serial.printf("[BTN] brightness %d/5\n", backlightLevel_);
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

    // Render the active page (main pet / info / clock). On page switch the
    // main view forces a full redraw so no stale pixels remain.
    static Page lastPage = Page::MAIN;
    if (page_ != lastPage) {
        lastPage = page_;
        lastShownState = static_cast<PetState>(0xFF);
        if (page_ == Page::INFO) lastInfoSig_[0] = '\0';
        if (page_ == Page::AGENTS) lastAgentsSig_[0] = '\0';
        if (page_ == Page::CLOCK) {
            // force the full clock to redraw on entry
            clockDateSig_[0] = '\0';
            clockLastHour_ = clockLastMin_ = clockLastSec_ = -1;
        }
    }
    // brightness overlay freezes the page underneath; drawn only when the
    // level changes (not every frame), so nothing flickers
    static bool overlayWasActive = false;
    static int lastOverlayLevel = -1;
    const bool overlayActive = (now < brightnessShowUntil_);
    if (overlayActive) {
        if (!overlayWasActive || backlightLevel_ != lastOverlayLevel) {
            drawBrightnessOverlay();
            lastOverlayLevel = backlightLevel_;
        }
    } else {
        lastOverlayLevel = -1;
        if (overlayWasActive) {
            if (page_ == Page::CLOCK) {
                clockDateSig_[0] = '\0';
                clockLastHour_ = clockLastMin_ = clockLastSec_ = -1;
            } else if (page_ == Page::INFO) {
                lastInfoSig_[0] = '\0';
            } else if (page_ == Page::AGENTS) {
                lastAgentsSig_[0] = '\0';
            } else {
                lastShownState = static_cast<PetState>(0xFF);
            }
        }
        if (page_ == Page::CLOCK) {
            drawClockPage(now);
        } else if (page_ == Page::INFO) {
            updateInfoScreen(now);
        } else if (page_ == Page::AGENTS) {
            updateAgentsPage();
        } else {
            if (lastShownState != pet.state() || lastShownState == static_cast<PetState>(0xFF)) {
                lastShownState = pet.state();
                drawStatusBar(pet.state());
                agentPanelDirty_ = true;   // full redraw: repaint the panel too
            }
            if (ble.agentsChanged()) {
                ble.clearAgentsChanged();
                agentPanelDirty_ = true;
            }
            if (agentPanelDirty_) {
                agentPanelDirty_ = false;
                drawAgentPanel();
            }
            pet.draw(tft, AGENT_PANEL_W, LAYOUT_PET_Y);
        }
    }
    overlayWasActive = overlayActive;
    frames++;

    if (now - lastFpsLog >= 5000) {
        const float fps = frames * 1000.0f / (now - lastFpsLog);
        Serial.printf("[PERF] fps=%.1f heap=%u\n", fps, ESP.getFreeHeap());
        lastFpsLog = now;
        frames = 0;
    }

    delay(16);
}
