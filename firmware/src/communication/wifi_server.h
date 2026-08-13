#pragma once

#include <Arduino.h>
#include <WebServer.h>

class WifiServer {
public:
    void begin();
    void update();

    bool isConnected() const;
    bool hasPendingState() const { return statePending_; }
    uint8_t pendingState() const { return pendingState_; }
    const char* pendingTask() const { return pendingTask_.c_str(); }
    void clearPendingState() { statePending_ = false; pendingTask_ = ""; }

private:
    WebServer server_{80};
    bool statePending_ = false;
    uint8_t pendingState_ = 0;
    String pendingTask_;
    String ssid_;
    String pass_;
    uint32_t nextRetryMs_ = 0;
    uint32_t backoffMs_ = 3000;

    void loadCredentials();
    void connectSta();
    void handleRoot();
    void handleState();
    void handleStatus();
    void handleWifiGet();
    void handleWifiPost();
};
