#include "wifi_server.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

#include "ble_manager.h"
#include "config/config.h"
#include "pet/pet_animation.h"
#include "pet/pet_state.h"

extern PetAnimation pet;
extern BleManager ble;
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
constexpr char NVS_LIST[] = "wifi_list";   // JSON array of {ssid,pass}
constexpr char NVS_SSID[] = "wifi_ssid";   // legacy single-network keys
constexpr char NVS_PASS[] = "wifi_pass";
constexpr uint32_t BACKOFF_MIN_MS = 3000;
constexpr uint32_t BACKOFF_MAX_MS = 60000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 8000;   // give each network this long

} // namespace

void WifiServer::loadCredentials() {
    nets_.clear();

    // New format: JSON array under wifi_list
    {
        Preferences pref;
        pref.begin(NVS_NS, true);
        const String list = pref.getString(NVS_LIST, "");
        pref.end();
        if (list.length() > 0) {
            JsonDocument doc;
            if (deserializeJson(doc, list) == DeserializationError::Ok) {
                for (JsonObject n : doc.as<JsonArray>()) {
                    const char* s = n["ssid"] | "";
                    const char* p = n["pass"] | "";
                    if (strlen(s) > 0) {
                        nets_.push_back({String(s), String(p)});
                    }
                }
            }
        }
    }

    // Migrate the legacy single-network keys into the list.
    if (nets_.empty()) {
        Preferences pref;
        pref.begin(NVS_NS, true);
        const String s = pref.getString(NVS_SSID, "");
        const String p = pref.getString(NVS_PASS, "");
        pref.end();
        if (!s.isEmpty()) {
            nets_.push_back({s, p});
        }
    }

    // Fall back to the compiled-in defaults (secrets.h) on first boot.
    if (nets_.empty()) {
        nets_.push_back({WIFI_SSID, WIFI_PASS});
    }

    if (netIndex_ >= (int)nets_.size()) netIndex_ = 0;
}

void WifiServer::saveNetworks() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& n : nets_) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = n.ssid;
        o["pass"] = n.pass;
    }
    String out;
    serializeJson(doc, out);

    Preferences pref;
    pref.begin(NVS_NS, false);
    pref.putString(NVS_LIST, out);
    pref.remove(NVS_SSID);
    pref.remove(NVS_PASS);
    pref.end();
}

void WifiServer::tryNetwork(int index) {
    if (index < 0 || index >= (int)nets_.size()) return;
    const WifiNet& n = nets_[index];
    netIndex_ = index;
    ssid_ = n.ssid;
    pass_ = n.pass;
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(n.ssid.c_str(), n.pass.c_str());
    connectStartedMs_ = millis();
    Serial.printf("[WIFI] STA trying %s\n", n.ssid.c_str());
}

void WifiServer::connectSta() {
    if (nets_.empty()) return;
    tryNetwork(netIndex_);
}

void WifiServer::begin() {
    loadCredentials();

    // AP always on + STA in the background rotating through saved networks.
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
}

bool WifiServer::isConnected() const {
    return staEnabled_ && WiFi.status() == WL_CONNECTED;
}

void WifiServer::update() {
    const uint32_t now = millis();

    if (staEnabled_ && !nets_.empty()) {
        if (reconnectRequested_) {
            // a network was just added/updated: switch to it now (non-blocking
            // for the web server, unlike calling WiFi.begin in the handler)
            reconnectRequested_ = false;
            connectSta();
            nextRetryMs_ = now + BACKOFF_MIN_MS;
        }
        const wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            backoffMs_ = BACKOFF_MIN_MS;
            nextRetryMs_ = now + BACKOFF_MIN_MS;
        } else if (now - connectStartedMs_ >= CONNECT_TIMEOUT_MS && now >= nextRetryMs_) {
            // Current network timed out or failed: rotate to the next one.
            Serial.printf("[WIFI] %s no connect, rotating\n", ssid_.c_str());
            const int next = (netIndex_ + 1) % (int)nets_.size();
            tryNetwork(next);
            nextRetryMs_ = now + backoffMs_;
            backoffMs_ = (backoffMs_ * 2 < BACKOFF_MAX_MS) ? backoffMs_ * 2 : BACKOFF_MAX_MS;
        }
    }

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
 .row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #21262d}
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
<div class="card"><h2>WiFi 设置（多网络自动切换）</h2>
 <form id="wf"><label>SSID</label><input id="s" autocomplete="off" placeholder="例如 ChinaUnicom-8DFA-2.4">
 <label>密码</label><input id="p" type="password" autocomplete="off">
 <button>添加 / 更新</button></form>
 <div id="netlist" style="margin-top:12px"></div>
 <div id="msg"></div>
</div>
<script>
function q(id){return document.getElementById(id)}
function fmtUptime(s){s=+s;var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}
function loadNets(){fetch('/api/wifi').then(function(r){return r.json()}).then(function(d){
 var el=q('netlist');el.innerHTML='';
 if(!d.nets.length){el.textContent='（未保存任何网络）';return}
 d.nets.forEach(function(s){
  var row=document.createElement('div');row.className='row';
  var span=document.createElement('span');span.textContent=s;
  var btn=document.createElement('button');btn.textContent='删除';
  btn.style.cssText='width:auto;margin:0;padding:4px 10px;background:#f85149';
  btn.onclick=function(){fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'remove',ssid:s})}).then(function(r){return r.text()}).then(function(t){q('msg').textContent=t;loadNets()})};
  row.appendChild(span);row.appendChild(btn);el.appendChild(row);
 });
}).catch(function(){})}
setInterval(function(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){
 q('state').textContent=d.state;
 q('ble').textContent=d.ble?'在线':'离线';
 q('wifi').textContent=d.wifi?(d.ssid+' · '+d.ip):'No WiFi';
 q('balance').textContent=d.balance;
 q('usage').textContent=d.usage;
 q('up').textContent=fmtUptime(d.uptime)+' · '+(d.heap/1024|0)+' KB';
}).catch(function(){})},3000);
q('wf').onsubmit=function(e){e.preventDefault();
 fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({ssid:q('s').value,pass:q('p').value})})
 .then(function(r){return r.text()}).then(function(t){q('msg').textContent=t;loadNets();q('p').value=''})};
loadNets();
</script></body></html>)HTML";
    server_.send(200, "text/html", html);
}

void WifiServer::handleState() {
    const String body = server_.arg("plain");
    if (body.isEmpty()) {
        server_.send(400, "text/plain", "empty body");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server_.send(400, "text/plain", "bad json");
        return;
    }
    // state accepts either a name ("WORKING", case-insensitive) or a number 0..6
    int state = -1;
    if (doc["state"].is<const char*>()) {
        const String name = doc["state"].as<const char*>();
        for (uint8_t i = 0; i <= static_cast<uint8_t>(PetState::SLEEP); ++i) {
            if (name.equalsIgnoreCase(petStateName(static_cast<PetState>(i)))) {
                state = static_cast<int>(i);
                break;
            }
        }
    } else if (doc["state"].is<int>()) {
        state = doc["state"].as<int>();
    }
    if (state < 0 || state > static_cast<int>(PetState::SLEEP)) {
        server_.send(400, "text/plain", "bad state");
        return;
    }
    pendingState_ = static_cast<uint8_t>(state);
    pendingTask_ = doc["task"].is<const char*>() ? doc["task"].as<const char*>() : "";
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
    doc["balance"] = ble.balanceText();
    doc["usage"] = usageText;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
}

void WifiServer::handleWifiGet() {
    JsonDocument doc;
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (const auto& n : nets_) {
        nets.add(n.ssid);   // ssids only; passwords stay on-device
    }
    doc["connected"] = isConnected();
    doc["current"] = isConnected() ? ssid_ : "";
    if (isConnected()) {
        doc["ip"] = WiFi.localIP().toString();
    }
    doc["ap"] = AP_SSID;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
}

void WifiServer::handleWifiPost() {
    const String body = server_.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server_.send(400, "text/plain", "bad json");
        return;
    }
    const char* action = doc["action"] | "";
    const char* ssid = doc["ssid"] | "";

    if (strcmp(action, "remove") == 0) {
        if (strlen(ssid) == 0) {
            server_.send(400, "text/plain", "ssid required");
            return;
        }
        for (auto it = nets_.begin(); it != nets_.end(); ++it) {
            if (it->ssid == ssid) {
                nets_.erase(it);
                break;
            }
        }
        saveNetworks();
        if (netIndex_ >= (int)nets_.size()) {
            netIndex_ = nets_.empty() ? 0 : (int)nets_.size() - 1;
        }
        server_.send(200, "text/plain", "removed");
        return;
    }
    if (strcmp(action, "clear") == 0) {
        nets_.clear();
        saveNetworks();
        server_.send(200, "text/plain", "cleared");
        return;
    }

    // add / update a network
    if (strlen(ssid) == 0) {
        server_.send(400, "text/plain", "ssid required");
        return;
    }
    const char* pass = doc["pass"] | "";
    bool updated = false;
    for (auto& n : nets_) {
        if (n.ssid == ssid) {
            n.pass = pass;
            updated = true;
            break;
        }
    }
    if (!updated) {
        nets_.push_back({String(ssid), String(pass)});
    }
    saveNetworks();
    if (!isConnected()) {
        // try the (newly) saved network first; the actual WiFi.begin happens in
        // update() so this handler returns immediately.
        netIndex_ = (int)nets_.size() - 1;
        backoffMs_ = BACKOFF_MIN_MS;
        reconnectRequested_ = true;
    }
    server_.send(200, "text/plain", "saved");
}
