#pragma once

#include <Arduino.h>
#include "pet/pet_state.h"

struct PetPacket {
    PetState state = PetState::IDLE;
    uint8_t progress = 255;
    uint8_t mood = 0;
    uint8_t animation = 0;
    uint32_t timestamp = 0;
};

class BleManager {
public:
    void begin();
    void update(uint32_t nowMs);
    bool isOnline() const;
    bool hasNewPacket() const { return hasNewPacket_; }
    PetPacket takePacket();
    bool taskChanged() const { return taskChanged_; }
    const char* taskText() const { return taskText_; }
    void clearTaskChanged() { taskChanged_ = false; }

    uint32_t usageTokens() const { return usageTokens_; }
    bool usageChanged() const { return usageChanged_; }
    void clearUsageChanged() { usageChanged_ = false; }

    static BleManager* instance() { return s_instance; }
    void onConnected();
    void onDisconnected();
    void onStateWrite(const uint8_t* data, size_t len);
    void onTaskWrite(const uint8_t* data, size_t len);
    void onCommandWrite(const uint8_t* data, size_t len);

private:
    static BleManager* s_instance;
    bool connected_ = false;
    uint32_t lastPacketMs_ = 0;
    bool hasNewPacket_ = false;
    PetPacket packet_;
    char taskText_[64] = "";
    bool taskChanged_ = false;
    uint32_t usageTokens_ = 0;
    bool usageChanged_ = false;
    uint8_t lastLoggedState_ = 0xFF;
};
