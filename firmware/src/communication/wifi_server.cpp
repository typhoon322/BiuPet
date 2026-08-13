#include "wifi_server.h"

#include <ArduinoOTA.h>
#include <errno.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config/config.h"
#include "pet/pet_state.h"

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "your-wifi-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-wifi-password"
#endif

namespace {

const char* AP_SSID = "CodexPet-AP";
const char* AP_PASS = "codexpet123";

constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t RECONNECT_GRACE_MS = 5000;
constexpr uint32_t AP_RETRY_INTERVAL_MS = 60000;

constexpr uint16_t FRAME_W = 128;
constexpr uint16_t FRAME_H = 128;
constexpr uint32_t FRAME_BYTES = FRAME_W * FRAME_H * 2;
constexpr uint32_t MAX_B64 = (FRAME_BYTES + 2) / 3 * 4 + 64;

int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t base64Decode(const char* in, size_t len, uint8_t* out, size_t outCap) {
    size_t oi = 0;
    int buf = 0, bits = 0;
    for (size_t i = 0; i < len && oi < outCap; ++i) {
        char c = in[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        int v = b64val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[oi++] = static_cast<uint8_t>((buf >> bits) & 0xFF);
        }
    }
    return oi;
}

String stateDir(uint8_t state) {
    return String("/skin/") + state;
}

void updateCount(uint8_t state, uint16_t count) {
    String path = stateDir(state) + "/count.txt";
    File f = LittleFS.open(path, "w");
    if (f) {
        f.print(count);
        f.close();
    }
}

} // namespace

void WifiServer::begin() {
    // Non-blocking connect: start joining the home network and let update()
    // drive the state machine so the pet animates immediately at boot.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    connectStartedMs_ = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WIFI] connecting to %s...\n", WIFI_SSID);

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/state", HTTP_POST, [this]() { handleState(); });
    server_.on("/api/frame", HTTP_POST, [this]() { handleFrame(); });
    server_.on("/api/delay", HTTP_POST, [this]() { handleDelay(); });
    server_.on("/api/clear", HTTP_POST, [this]() { handleClear(); });
    server_.begin();

    ArduinoOTA.setHostname("codex-pet");
    ArduinoOTA.onStart([]() { Serial.println("[OTA] update start"); });
    ArduinoOTA.onEnd([]() { Serial.println("[OTA] update end"); });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] error %d\n", static_cast<int>(err));
    });
    ArduinoOTA.begin();
}

void WifiServer::update() {
    const uint32_t now = millis();
    switch (mode_) {
        case Mode::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                mode_ = Mode::Station;
                lastConnectedMs_ = now;
                Serial.printf("[WIFI] STA connected, IP %s\n",
                              WiFi.localIP().toString().c_str());
            } else if (now - connectStartedMs_ > CONNECT_TIMEOUT_MS) {
                mode_ = Mode::FallbackAp;
                WiFi.mode(WIFI_AP);
                WiFi.softAP(AP_SSID, AP_PASS);
                Serial.printf("[WIFI] STA timeout (status=%d), AP %s IP %s\n",
                              static_cast<int>(WiFi.status()), AP_SSID,
                              WiFi.softAPIP().toString().c_str());
            }
            break;

        case Mode::Station:
            if (WiFi.status() != WL_CONNECTED) {
                if (now - lastConnectedMs_ > RECONNECT_GRACE_MS) {
                    Serial.printf("[WIFI] link lost, reconnecting... (status=%d)\n",
                                  static_cast<int>(WiFi.status()));
                    WiFi.disconnect();
                    WiFi.begin(WIFI_SSID, WIFI_PASS);
                    connectStartedMs_ = now;
                    mode_ = Mode::Connecting;
                }
            } else {
                lastConnectedMs_ = now;
            }
            break;

        case Mode::FallbackAp:
            // Retry the home network periodically; clients on the AP
            // keep working while we are away.
            if (now - connectStartedMs_ > AP_RETRY_INTERVAL_MS) {
                Serial.println("[WIFI] retrying home network from AP...");
                WiFi.mode(WIFI_STA);
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                connectStartedMs_ = now;
                mode_ = Mode::Connecting;
            }
            break;
    }

    ArduinoOTA.handle();
    server_.handleClient();
}

void WifiServer::handleRoot() {
    String html = "<html><body><h1>CodexPet</h1>"
                  "<p>State: <b>POST /api/state</b> JSON {state,task}</p>"
                  "<p>Skin: <b>POST /api/frame?state=0..6&amp;index=N</b> base64 RGB565 128x128</p>"
                  "<p>Clear: <b>POST /api/clear?state=N</b></p></body></html>";
    server_.send(200, "text/html", html);
}

void WifiServer::handleState() {
    String body = server_.arg("plain");
    if (body.isEmpty()) {
        server_.send(400, "text/plain", "empty body");
        return;
    }
    // minimal JSON parse: "state":"working" or numeric
    int state = -1;
    int sIdx = body.indexOf("\"state\"");
    if (sIdx >= 0) {
        String sub = body.substring(sIdx);
        int colon = sub.indexOf(':');
        String val = sub.substring(colon + 1);
        val.trim();
        if (val.startsWith("\"")) {
            val = val.substring(1);
            int q = val.indexOf('"');
            if (q >= 0) val = val.substring(0, q);
            for (uint8_t i = 0; i <= static_cast<uint8_t>(PetState::SLEEP); ++i) {
                if (val.equalsIgnoreCase(petStateName(static_cast<PetState>(i)))) {
                    state = i;
                    break;
                }
            }
        } else {
            state = val.toInt();
        }
    }
    if (state < 0 || state > static_cast<int>(PetState::SLEEP)) {
        server_.send(400, "text/plain", "bad state");
        return;
    }
    pendingState_ = static_cast<uint8_t>(state);
    pendingTask_ = "";
    int tIdx = body.indexOf("\"task\"");
    if (tIdx >= 0) {
        String sub = body.substring(tIdx);
        int colon = sub.indexOf(':');
        String val = sub.substring(colon + 1);
        val.trim();
        if (val.startsWith("\"")) {
            val = val.substring(1);
            int q = val.indexOf('"');
            if (q >= 0) val = val.substring(0, q);
            pendingTask_ = val;
        }
    }
    statePending_ = true;
    server_.send(200, "text/plain", "ok");
}

void WifiServer::handleFrame() {
    if (!server_.hasArg("state") || !server_.hasArg("index")) {
        server_.send(400, "text/plain", "missing state/index");
        return;
    }
    const int state = server_.arg("state").toInt();
    const int index = server_.arg("index").toInt();
    if (state < 0 || state > static_cast<int>(PetState::SLEEP) || index < 0 || index > 63) {
        server_.send(400, "text/plain", "bad params");
        return;
    }
    String body = server_.arg("plain");
    if (body.length() > MAX_B64) {
        server_.send(413, "text/plain", "too large");
        return;
    }
    uint8_t* frame = static_cast<uint8_t*>(malloc(FRAME_BYTES));
    if (frame == nullptr) {
        server_.send(503, "text/plain", "no memory");
        return;
    }
    size_t got = base64Decode(body.c_str(), body.length(), frame, FRAME_BYTES);
    if (got != FRAME_BYTES) {
        free(frame);
        server_.send(400, "text/plain", "bad frame size");
        return;
    }
    String dir = stateDir(static_cast<uint8_t>(state));
    LittleFS.mkdir("/skin");
    LittleFS.mkdir(dir);
    String path = dir + "/" + index + ".rgb565";
    File f = LittleFS.open(path, "w");
    if (!f) {
        String msg = "write fail path=" + path +
                     " used=" + String((unsigned)LittleFS.usedBytes()) +
                     " total=" + String((unsigned)LittleFS.totalBytes()) +
                     " errno=" + String((int)errno);
        free(frame);
        server_.send(507, "text/plain", msg);
        return;
    }
    f.write(frame, FRAME_BYTES);
    f.close();
    free(frame);

    // update count = max(index+1, existing)
    String countPath = dir + "/count.txt";
    uint16_t count = index + 1;
    File cf = LittleFS.open(countPath, "r");
    if (cf) {
        int c = cf.readStringUntil('\n').toInt();
        if (c > count) count = c;
        cf.close();
    }
    updateCount(static_cast<uint8_t>(state), count);
    skinPending_ = true;
    server_.send(200, "text/plain", "ok");
}

void WifiServer::handleDelay() {
    if (!server_.hasArg("state")) {
        server_.send(400, "text/plain", "missing state");
        return;
    }
    const int state = server_.arg("state").toInt();
    if (state < 0 || state > static_cast<int>(PetState::SLEEP)) {
        server_.send(400, "text/plain", "bad state");
        return;
    }
    const int delay = server_.arg("plain").toInt();
    if (delay < 20 || delay > 5000) {
        server_.send(400, "text/plain", "delay must be 20..5000ms");
        return;
    }
    String dir = stateDir(static_cast<uint8_t>(state));
    LittleFS.mkdir(dir);
    File f = LittleFS.open(dir + "/delay_ms.txt", "w");
    if (!f) {
        server_.send(507, "text/plain", "write fail");
        return;
    }
    f.print(delay);
    f.close();
    skinPending_ = true;
    server_.send(200, "text/plain", "ok");
}

void WifiServer::handleClear() {
    if (!server_.hasArg("state")) {
        server_.send(400, "text/plain", "missing state");
        return;
    }
    const int state = server_.arg("state").toInt();
    if (state < 0 || state > static_cast<int>(PetState::SLEEP)) {
        server_.send(400, "text/plain", "bad state");
        return;
    }
    String dir = stateDir(static_cast<uint8_t>(state));
    File root = LittleFS.open(dir);
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            LittleFS.remove(f.path());
            f = root.openNextFile();
        }
    }
    server_.send(200, "text/plain", "ok");
}
