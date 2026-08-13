#include "net/deepseek_balance.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#include "config/config.h"

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#endif

#ifndef DEEPSEEK_API_KEY
#define DEEPSEEK_API_KEY ""
#endif

namespace {

constexpr char BALANCE_URL[] = "https://api.deepseek.com/user/balance";
constexpr uint32_t FETCH_INTERVAL_MS = 30000;  // every half minute
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
constexpr char NVS_NS[] = "codepet";
constexpr char NVS_KEY[] = "ds_key";

}  // namespace

void DeepSeekBalance::begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return;
    }

    // Compile-time key (secrets.h) is persisted once so a later OTA keeps it.
    Preferences pref;
    pref.begin(NVS_NS, true);
    String key = pref.getString(NVS_KEY, "");
    pref.end();
    if (key.isEmpty() && strlen(DEEPSEEK_API_KEY) > 0) {
        key = DEEPSEEK_API_KEY;
        pref.begin(NVS_NS, false);
        pref.putString(NVS_KEY, key);
        pref.end();
    }
    apiKey_ = key;

    if (apiKey_.isEmpty()) {
        setResult("--");
        Serial.println("[DS] no API key configured");
        return;
    }

    xTaskCreatePinnedToCore(taskMain, "dsBalance", 12288, this, 1, &task_, 1);
}

void DeepSeekBalance::taskMain(void* arg) {
    auto* self = static_cast<DeepSeekBalance*>(arg);
    uint32_t lastFetchMs = 0;
    for (;;) {
        const uint32_t now = millis();
        if (WiFi.status() == WL_CONNECTED &&
            (lastFetchMs == 0 || now - lastFetchMs >= FETCH_INTERVAL_MS)) {
            self->fetchAndUpdate();
            lastFetchMs = millis();
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void DeepSeekBalance::fetchAndUpdate() {
    WiFiClientSecure client;
    client.setInsecure();  // same tradeoff as EnvMonitor
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    esp_task_wdt_reset();
    if (!http.begin(client, BALANCE_URL)) {
        if (!valid_) setResult("--");
        http.end();
        return;
    }
    http.addHeader("Authorization", "Bearer " + apiKey_);
    http.addHeader("Accept", "application/json");

    esp_task_wdt_reset();
    const int code = http.GET();
    esp_task_wdt_reset();
    if (code != HTTP_CODE_OK) {
        if (!valid_) setResult("--");
        http.end();
        return;
    }
    const String payload = http.getString();
    http.end();
    esp_task_wdt_reset();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        if (!valid_) setResult("--");
        return;
    }

    JsonArray infos = doc["balance_infos"].as<JsonArray>();
    if (infos.isNull() || infos.size() == 0) {
        if (!valid_) setResult("--");
        return;
    }
    JsonObject item = infos[0];
    const char* total = item["total_balance"] | "0";
    const bool available = doc["is_available"] | false;

    char out[32];
    // compact value for the corner: "75.78" or "!75.78"
    snprintf(out, sizeof(out), "%s%s", available ? "" : "!", total);
    setResult(out);
    valid_ = true;
}

void DeepSeekBalance::setResult(const String& text) {
    if (mutex_ == nullptr) {
        return;
    }
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        strncpy(text_, text.c_str(), sizeof(text_) - 1);
        text_[sizeof(text_) - 1] = '\0';
        xSemaphoreGive(mutex_);
    }
}

const char* DeepSeekBalance::displayText() {
    if (mutex_ == nullptr) {
        return readBuf_;
    }
    if (xSemaphoreTake(mutex_, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        strncpy(readBuf_, text_, sizeof(readBuf_) - 1);
        readBuf_[sizeof(readBuf_) - 1] = '\0';
        xSemaphoreGive(mutex_);
    }
    return readBuf_;
}
