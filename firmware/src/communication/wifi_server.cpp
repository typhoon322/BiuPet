#include "wifi_server.h"

#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Preferences.h>
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
constexpr char NVS_NS[] = "codepet";
constexpr char NVS_SSID[] = "wifi_ssid";
constexpr char NVS_PASS[] = "wifi_pass";
constexpr uint32_t BACKOFF_MIN_MS = 3000;
constexpr uint32_t BACKOFF_MAX_MS = 60000;

} // namespace

void WifiServer::loadCredentials() {
    Preferences pref;
    pref.begin(NVS_NS, true);
    ssid_ = pref.getString(NVS_SSID, "");
    pass_ = pref.getString(NVS_PASS, "");
    pref.end();
    if (ssid_.isEmpty()) {
        ssid_ = WIFI_SSID;
        pass_ = WIFI_PASS;
    }
}

void WifiServer::connectSta() {
    if (ssid_.isEmpty()) {
        return;
    }
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(ssid_.c_str(), pass_.c_str());
    Serial.printf("[WIFI] STA connecting to %s\n", ssid_.c_str());
}

void WifiServer::begin() {
    loadCredentials();

    // EnvMonitor-style: AP always on, STA connects in the background with
    // backoff retries, so the pet never loses its setup/control surface.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WIFI] AP %s IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    connectSta();
    nextRetryMs_ = millis() + BACKOFF_MIN_MS;

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/state", HTTP_POST, [this]() { handleState(); });
    server_.on("/api/wifi", HTTP_GET, [this]() { handleWifiGet(); });
    server_.on("/api/wifi", HTTP_POST, [this]() { handleWifiPost(); });
    server_.begin();

    ArduinoOTA.setHostname("codex-pet");
    ArduinoOTA.onStart([]() { Serial.println("[OTA] update start"); });
    ArduinoOTA.onEnd([]() { Serial.println("[OTA] update end"); });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] error %d\n", static_cast<int>(err));
    });
    ArduinoOTA.begin();
}

bool WifiServer::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

void WifiServer::update() {
    const uint32_t now = millis();

    // STA reconnect with backoff, mirroring EnvMonitor's wifi_manager.
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        backoffMs_ = BACKOFF_MIN_MS;
        nextRetryMs_ = now + BACKOFF_MIN_MS;
    } else if (st != WL_IDLE_STATUS && st != WL_SCAN_COMPLETED) {
        if (now >= nextRetryMs_) {
            Serial.printf("[WIFI] status=%d, retrying STA\n", static_cast<int>(st));
            connectSta();
            nextRetryMs_ = now + backoffMs_;
            backoffMs_ = (backoffMs_ * 2 < BACKOFF_MAX_MS) ? backoffMs_ * 2 : BACKOFF_MAX_MS;
        }
    }

    ArduinoOTA.handle();
    server_.handleClient();
}

void WifiServer::handleRoot() {
    String html = "<html><body><h1>CodexPet</h1>"
                  "<p>State: <b>POST /api/state</b> JSON {state,task}</p>"
                  "<h2>WiFi</h2>"
                  "<form id=wf><label>SSID</label><input id=s><br>"
                  "<label>Password</label><input id=p type=password><br>"
                  "<button>Save &amp; reconnect</button></form>"
                  "<div id=msg></div>"
                  "<script>"
                  "fetch('/api/wifi').then(r=>r.json()).then(d=>{"
                  "document.getElementById('s').value=d.ssid;"
                  "document.getElementById('msg').textContent= d.connected?('OK '+d.ip):'not connected';});"
                  "document.getElementById('wf').onsubmit=e=>{e.preventDefault();"
                  "fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},"
                  "body:JSON.stringify({ssid:document.getElementById('s').value,"
                  "pass:document.getElementById('p').value})}).then(r=>r.text()).then(t=>{"
                  "document.getElementById('msg').textContent=t;});};"
                  "</script></body></html>";
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

void WifiServer::handleWifiGet() {
    String json = "{\"ssid\":\"" + ssid_ + "\",\"connected\":" +
                  String(isConnected() ? "true" : "false");
    if (isConnected()) {
        json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    }
    json += ",\"ap\":\"" + String(AP_SSID) + "\"}";
    server_.send(200, "application/json", json);
}

void WifiServer::handleWifiPost() {
    const String body = server_.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server_.send(400, "text/plain", "bad json");
        return;
    }
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    if (strlen(ssid) == 0) {
        server_.send(400, "text/plain", "ssid required");
        return;
    }
    Preferences pref;
    pref.begin(NVS_NS, false);
    pref.putString(NVS_SSID, ssid);
    pref.putString(NVS_PASS, pass);
    pref.end();

    ssid_ = ssid;
    pass_ = pass;
    backoffMs_ = BACKOFF_MIN_MS;
    nextRetryMs_ = 0;
    connectSta();
    server_.send(200, "text/plain", "saved, reconnecting...");
}
