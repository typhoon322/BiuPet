#include "ble_manager.h"

#include <NimBLEDevice.h>
#include "config/config.h"

namespace {

const char* SERVICE_UUID = "CDD4DFFB-2FD0-4F2D-9A87-C7D2535B59E0";
const char* STATE_CHAR_UUID = "DA3CDABA-E192-460B-ACF3-B2C59C6A3EE0";
const char* STATUS_CHAR_UUID = "7497D0A0-FE42-4E2D-B28E-93D083A7CD68";
const char* COMMAND_CHAR_UUID = "7C9DA1DE-8FE4-4B95-A366-51EA94E3010C";
const char* TASK_CHAR_UUID = "9578BE09-6CA5-4221-8F83-E26996838F86";

constexpr uint32_t OFFLINE_TIMEOUT_MS = 15000;

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] connected: %s\n", connInfo.getAddress().toString().c_str());
        if (BleManager::instance()) BleManager::instance()->onConnected();
    }
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("[BLE] disconnected: %s reason=%d\n", connInfo.getAddress().toString().c_str(), reason);
        if (BleManager::instance()) BleManager::instance()->onDisconnected();
        NimBLEDevice::startAdvertising();
    }
};

class StateCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] onWrite called len=%u\n", (unsigned)chr->getValue().size());
        const std::string value = chr->getValue();
        if (BleManager::instance()) {
            BleManager::instance()->onStateWrite(
                reinterpret_cast<const uint8_t*>(value.data()), value.size());
        }
    }
};

class TaskCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        const std::string value = chr->getValue();
        if (BleManager::instance()) {
            BleManager::instance()->onTaskWrite(
                reinterpret_cast<const uint8_t*>(value.data()), value.size());
        }
    }
};

} // namespace

BleManager* BleManager::s_instance = nullptr;

void BleManager::begin() {
    s_instance = this;

    NimBLEDevice::init(Config::BleDeviceName);
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* service = server->createService(SERVICE_UUID);

    NimBLECharacteristic* stateChar = service->createCharacteristic(
        STATE_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    stateChar->setCallbacks(new StateCallbacks());
    stateChar->setValue((uint8_t*)nullptr, 0);

    service->createCharacteristic(STATUS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* commandChar = service->createCharacteristic(
        COMMAND_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    commandChar->setCallbacks(new StateCallbacks()); // placeholder, parsed in main if needed

    NimBLECharacteristic* taskChar = service->createCharacteristic(
        TASK_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    taskChar->setCallbacks(new TaskCallbacks());

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setName(Config::BleDeviceName);
    advertising->enableScanResponse(true);
    NimBLEDevice::startAdvertising();

    lastPacketMs_ = millis();
    Serial.println("[BLE] advertising as " + String(Config::BleDeviceName));
}

void BleManager::update(uint32_t nowMs) {
    if (connected_ && nowMs - lastPacketMs_ > OFFLINE_TIMEOUT_MS) {
        Serial.println("[BLE] heartbeat timeout -> OFFLINE");
        connected_ = false;
    }
}

bool BleManager::isOnline() const {
    return connected_;
}

void BleManager::onConnected() {
    connected_ = true;
    lastPacketMs_ = millis();
}

void BleManager::onDisconnected() {
    connected_ = false;
}

void BleManager::onStateWrite(const uint8_t* data, size_t len) {
    lastPacketMs_ = millis();
    if (len < 2) {
        Serial.println("[BLE] short state packet");
        return;
    }
    if (data[0] != 0x01) {
        Serial.printf("[BLE] unknown protocol version %u\n", data[0]);
        return;
    }
    const uint8_t state = data[1];
    if (state > static_cast<uint8_t>(PetState::SLEEP)) {
        Serial.printf("[BLE] invalid state %u\n", state);
        return;
    }
    packet_.state = static_cast<PetState>(state);
    packet_.progress = (len >= 3) ? data[2] : 255;
    packet_.mood = (len >= 4) ? data[3] : 0;
    packet_.animation = (len >= 5) ? data[4] : 0;
    if (len >= 10) {
        packet_.timestamp = data[6] | (data[7] << 8) | (data[8] << 16) |
                            (static_cast<uint32_t>(data[9]) << 24);
    }
    hasNewPacket_ = true;
    Serial.printf("[BLE] state packet: state=%u progress=%u flags=0x%02x\n",
                  static_cast<uint8_t>(packet_.state), packet_.progress, data[5]);
}

void BleManager::onTaskWrite(const uint8_t* data, size_t len) {
    lastPacketMs_ = millis();
    const size_t n = (len < sizeof(taskText_) - 1) ? len : sizeof(taskText_) - 1;
    memcpy(taskText_, data, n);
    taskText_[n] = '\0';
    taskChanged_ = true;
    Serial.printf("[BLE] task text: %s\n", taskText_);
}

PetPacket BleManager::takePacket() {
    hasNewPacket_ = false;
    return packet_;
}
