#include "wifi_server.h"

#include <ArduinoOTA.h>
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
                  "<p>OTA: ArduinoOTA hostname codex-pet</p></body></html>";
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
