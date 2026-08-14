#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <pgmspace.h>
#include <SPI.h>

#include "config/config.h"
#include "pet/pet_state.h"
#include "pet/pet_animation.h"
#include "communication/ble_manager.h"
#include "communication/wifi_server.h"
#include "storage/pet_stats.h"
#include "ui/chinese_text.h"

Adafruit_ST7789 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
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

const uint16_t DS_BLUE = ((77 & 0xF8) << 8) | ((107 & 0xFC) << 3) | (254 >> 3);

// DeepSeek official whale mark (1-bit bitmap, ported from EnvMonitor),
// brand blue #4D6BFE. Drawn into a small RAM canvas and blitted with
// writePixels only -- shape calls on the TFT deadlock this SPI stack.
static constexpr int kDsWhaleBmpW = 52;
static constexpr int kDsWhaleBmpH = 32;
static constexpr int kDsWhaleBmpStride = 7;

static const uint8_t kDsWhaleBmp[] PROGMEM = {
    0x00, 0x01, 0x80, 0x60, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0xFE, 0x00, 0x00, 0x00, 0xC0, 0x07,
    0xC0, 0x7F, 0xFF, 0xE0, 0x00, 0xE0, 0x1F, 0xC0, 0x3F, 0xFF, 0xF8, 0x00, 0xFF, 0x3F, 0xC0, 0x7F,
    0xFF, 0xFE, 0x00, 0xFF, 0xFF, 0xC0, 0xFF, 0xFF, 0xFF, 0x00, 0x7F, 0xFF, 0x83, 0xFF, 0xFF, 0xFF,
    0x80, 0x7F, 0xFF, 0x87, 0xFF, 0xFF, 0xFF, 0xC0, 0x3F, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0xC0, 0x1F,
    0xFE, 0x1F, 0xFF, 0xFF, 0xFF, 0xE0, 0x07, 0xFC, 0x7F, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0xFC, 0xFF,
    0xFF, 0xFF, 0xFF, 0xF0, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0xF0, 0x00, 0xFF, 0xFC, 0xFF, 0xFC,
    0x00, 0xF0, 0x00, 0x7F, 0xF3, 0xFF, 0xF0, 0x00, 0xF0, 0x00, 0x7F, 0xE3, 0xFF, 0xC0, 0x01, 0xF0,
    0x00, 0x7F, 0xE3, 0xFF, 0x80, 0x01, 0xF0, 0x00, 0x7F, 0xC7, 0xFF, 0x00, 0x01, 0xF0, 0x00, 0x3F,
    0xFF, 0xFE, 0x00, 0x03, 0xF0, 0x00, 0x3F, 0xFF, 0xFC, 0x00, 0x03, 0xE0, 0x00, 0x1F, 0xFF, 0xF8,
    0x00, 0x07, 0xE0, 0x00, 0x0F, 0xFF, 0xF0, 0x00, 0x07, 0xC0, 0x00, 0x0F, 0xFF, 0xE0, 0x00, 0x0F,
    0xC0, 0x00, 0x07, 0xFF, 0xE0, 0xE0, 0x1F, 0x80, 0x00, 0x03, 0xFF, 0xC3, 0xE0, 0x7F, 0x80, 0x00,
    0x01, 0xFF, 0x87, 0xE0, 0xFF, 0x00, 0x00, 0x0F, 0xFF, 0x1F, 0xC3, 0xFE, 0x00, 0x00, 0x1F, 0xFC,
    0x7F, 0xFF, 0xF8, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0xF0, 0x00, 0x00, 0x07, 0xCF, 0xFF, 0xFF,
    0xE0, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFC, 0x00, 0x00,
};

static GFXcanvas16 whaleCanvas_{26, 16};

static void precomputeWhale() {
    whaleCanvas_.fillScreen(0x0000);
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 26; ++c) {
            bool set = false;
            for (int dy = 0; dy < 2 && !set; ++dy) {
                for (int dx = 0; dx < 2 && !set; ++dx) {
                    const int srcC = c * 2 + dx;
                    const int srcR = r * 2 + dy;
                    const uint8_t byte =
                        pgm_read_byte(&kDsWhaleBmp[srcR * kDsWhaleBmpStride + srcC / 8]);
                    if (byte & (0x80 >> (srcC % 8))) {
                        set = true;
                    }
                }
            }
            if (set) {
                whaleCanvas_.drawPixel(c, r, DS_BLUE);
            }
        }
    }
}

static void drawWhale(Adafruit_ST7789& tft, int16_t x, int16_t y) {
    const uint16_t* buf = whaleCanvas_.getBuffer();
    tft.startWrite();
    tft.setAddrWindow(x, y, 26, 16);
    uint32_t total = 26 * 16;
    uint32_t off = 0;
    while (off < total) {
        const uint32_t n = (total - off > 1024) ? 1024 : (total - off);
        tft.writePixels(const_cast<uint16_t*>(buf + off), n);
        off += n;
    }
    tft.endWrite();
}

void drawStatusBar(PetState state) {
    tft.fillRect(0, 0, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 7);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.print("CODEX PET");

    // state name (middle; "CODEX PET" at size 2 ends at x=136)
    tft.setCursor(140, 9);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print(petStateName(state));

    const bool online = ble.isOnline() || wifi.isConnected();
    const char* onlineLabel = online ? "ONLINE" : "OFFLINE";
    tft.setCursor(196, 9);
    tft.setTextColor(online ? ST77XX_GREEN : ST77XX_RED);
    tft.setTextSize(1);
    tft.print(onlineLabel);

    tft.setCursor(252, 9);
    tft.setTextColor(wifi.isConnected() ? ST77XX_GREEN : ST77XX_RED);
    tft.setTextSize(1);
    tft.print(wifi.isConnected() ? "WiFi" : "No WiFi");

    const auto& st = stats.stats();
    char lvLine[48];
    snprintf(lvLine, sizeof(lvLine), "Lv.%u exp %u/%u tasks:%u",
             st.level, stats.expInLevel(), PetStatsStore::EXP_PER_LEVEL, st.tasksCompleted);
    tft.fillRect(0, 28, 320, 10, ST77XX_BLACK);
    tft.setCursor(16, 29);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.print(lvLine);

    // task line just above the bottom bar
    tft.fillRect(0, 198, 320, 14, ST77XX_BLACK);
    drawChineseText(tft, 16, 198, bottomText, ST77XX_CYAN, 296);

    // bottom bar: usage bottom-left, whale + balance bottom-right
    tft.fillRect(0, 214, 320, 26, ST77XX_BLACK);
    tft.setCursor(16, 220);
    tft.setTextColor(ST77XX_MAGENTA);
    tft.setTextSize(1);
    tft.print(usageText);

    // balance: fixed 6-char width, right-aligned
    char bal6[7];
    strncpy(bal6, ble.balanceText(), 6);
    bal6[6] = '\0';
    char balFixed[7];
    snprintf(balFixed, sizeof(balFixed), "%6s", bal6);
    tft.setCursor(276, 220);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.print(balFixed);
    drawWhale(tft, 246, 215);  // whale left of the balance, vertically centered
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { delay(10); }
    Serial.println("\n=== CodexPet phase2: BLE ===");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 140);  // full brightness washes black out on this panel

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

    precomputeWhale();
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
            snprintf(bottomText, sizeof(bottomText), "task: %s", wifi.pendingTask());
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

    pet.draw(tft, 0, 64);
    frames++;

    if (now - lastFpsLog >= 5000) {
        const float fps = frames * 1000.0f / (now - lastFpsLog);
        Serial.printf("[PERF] fps=%.1f heap=%u\n", fps, ESP.getFreeHeap());
        lastFpsLog = now;
        frames = 0;
    }

    delay(16);
}
