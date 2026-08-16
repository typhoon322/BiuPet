#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <vector>

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
    struct WifiNet {
        String ssid;
        String pass;
    };

    WebServer server_{80};
    bool statePending_ = false;
    uint8_t pendingState_ = 0;
    String pendingTask_;
    std::vector<WifiNet> nets_;   // saved networks, rotated until one connects
    int netIndex_ = 0;            // index of the network currently being tried
    String ssid_;                 // currently selected / connected ssid
    String pass_;
    bool staEnabled_ = true;
    uint32_t nextRetryMs_ = 0;
    uint32_t backoffMs_ = 3000;
    uint32_t connectStartedMs_ = 0;
    bool reconnectRequested_ = false;   // set by web handler; acted on in update()

    void loadCredentials();
    void saveNetworks();
    void tryNetwork(int index);
    void connectSta();
    void handleRoot();
    void handleState();
    void handleStatus();
    void handleWifiGet();
    void handleWifiPost();
};
