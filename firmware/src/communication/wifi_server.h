#pragma once

#include <Arduino.h>
#include <WebServer.h>

class WifiServer {
public:
    void begin();
    void update();

    // true when any WiFi link (station or fallback AP) is up
    bool isConnected() const { return mode_ != Mode::Connecting; }
    bool hasPendingState() const { return statePending_; }
    uint8_t pendingState() const { return pendingState_; }
    const char* pendingTask() const { return pendingTask_.c_str(); }
    void clearPendingState() { statePending_ = false; pendingTask_ = ""; }

private:
    WebServer server_{80};
    bool statePending_ = false;
    uint8_t pendingState_ = 0;
    String pendingTask_;

    enum class Mode : uint8_t { Connecting, Station, FallbackAp };
    Mode mode_ = Mode::Connecting;
    uint32_t connectStartedMs_ = 0;
    uint32_t lastConnectedMs_ = 0;

    void handleRoot();
    void handleState();
};
