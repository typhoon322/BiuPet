#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Fetches the DeepSeek account balance every 30s in a background task
// (mirrors EnvMonitor's on-device deepseek monitor, minus the animation).
class DeepSeekBalance {
public:
    void begin();
    // Thread-safe snapshot for the UI loop, e.g. "ds ¥110.00".
    const char* displayText();

private:
    static void taskMain(void* arg);
    void fetchAndUpdate();
    void setResult(const String& text);

    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    String apiKey_;
    char text_[16] = "--";
    char readBuf_[16] = "--";
    bool valid_ = false;
};
