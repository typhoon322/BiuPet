#include "wifi_server.h"

#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

#include "ble_manager.h"
#include "config/config.h"
#include "net/deepseek_balance.h"
#include "pet/pet_animation.h"
#include "pet/pet_state.h"

extern PetAnimation pet;
extern BleManager ble;
extern DeepSeekBalance ds;
extern char usageText[];

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
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
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
    const char* html = R"HTML(<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CodexPet</title>
<style>
 body{background:#0e1116;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px}
 .card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:18px;margin-bottom:16px;max-width:520px}
 h1{font-size:20px;margin:0 0 4px;color:#fff}
 h2{font-size:15px;margin:0 0 12px;color:#8b949e;font-weight:600}
 .row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #21262d}
 .row:last-child{border-bottom:none}
 .ok{color:#3fb950}.bad{color:#f85149}
 .val{font-weight:600}
 label{display:block;margin:10px 0 4px;font-size:13px;color:#8b949e}
 input{width:100%;box-sizing:border-box;background:#0d1117;border:1px solid #30363d;border-radius:8px;color:#e6edf3;padding:9px 10px;font-size:14px}
 button{margin-top:14px;width:100%;background:#4d6bfe;border:none;border-radius:8px;color:#fff;padding:10px;font-size:15px;font-weight:600}
 button:active{opacity:.8}
 #msg{margin-top:10px;font-size:13px;color:#3fb950}
</style></head><body>
<div class="card"><h1>&#128049; CodexPet</h1><h2>宠物状态</h2>
 <div class="row"><span>状态</span><span class="val" id="state">-</span></div>
 <div class="row"><span>BLE</span><span class="val" id="ble">-</span></div>
 <div class="row"><span>WiFi</span><span class="val" id="wifi">-</span></div>
 <div class="row"><span>DeepSeek 余额</span><span class="val" id="balance">-</span></div>
 <div class="row"><span>今日用量</span><span class="val" id="usage">-</span></div>
 <div class="row"><span>运行时间 / 内存</span><span class="val" id="up">-</span></div>
</div>
<div class="card"><h2>WiFi 设置</h2>
 <form id="wf"><label>SSID</label><input id="s" autocomplete="off">
 <label>密码</label><input id="p" type="password">
 <button>保存并重连</button></form>
 <div id="msg"></div>
</div>
<script>
function q(id){return document.getElementById(id)}
function fmtUptime(s){s=+s;var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}
setInterval(function(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){
 q('state').textContent=d.state;
 q('ble').textContent=d.ble?'在线':'离线';
 q('wifi').textContent=d.wifi?(d.ssid+' · '+d.ip):'No WiFi';
 q('balance').textContent=d.balance;
 q('usage').textContent=d.usage;
 q('up').textContent=fmtUptime(d.uptime)+' · '+(d.heap/1024|0)+' KB';
}).catch(function(){})},3000);
fetch('/api/wifi').then(function(r){return r.json()}).then(function(d){q('s').value=d.ssid});
q('wf').onsubmit=function(e){e.preventDefault();
 fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({ssid:q('s').value,pass:q('p').value})})
  .then(function(r){return r.text()}).then(function(t){q('msg').textContent=t});};
</script></body></html>)HTML";
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

void WifiServer::handleStatus() {
    JsonDocument doc;
    doc["state"] = petStateName(pet.state());
    doc["ble"] = ble.isOnline();
    doc["wifi"] = isConnected();
    doc["ssid"] = isConnected() ? ssid_ : "";
    doc["ip"] = isConnected() ? WiFi.localIP().toString() : "";
    doc["balance"] = ds.displayText();
    doc["usage"] = usageText;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
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
