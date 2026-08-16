#pragma once

#include <Arduino.h>

// LiPo battery monitoring for the LilyGo T-Display-S3 (320x170).
// GPIO4 = battery voltage via 1/2 divider. The divider sits on the USB-OR'd
// rail, so it reads ~4.8V on USB and the real cell voltage (3.0..4.2V) on
// battery power. The TP4065 charger exposes no charge-status pin, so while on
// USB the % is ESTIMATED from charge time (250mAh @ 500mA ~ 20s per %) and
// charging stops when the estimate reaches 100%.
#if defined(DISPLAY_8080)

class Battery {
public:
    void begin();
    void update(uint32_t nowMs);   // sample every ~2s, smooth the ADC

    int percent() const { return displayPct_; }        // real on battery, estimated on USB
    uint16_t millivolts() const { return mv_; }
    bool charging() const { return charging_; }
    bool usbPresent() const { return usbPresent_; }

private:
    static constexpr int kAdcPin = 4;
    static constexpr uint32_t kSecPerPct = 20;   // ~500mA into a 250mAh cell
    uint32_t lastReadMs_ = 0;
    uint16_t mv_ = 0;
    int realPercent_ = -1;      // last actual % measured on battery (persisted)
    int displayPct_ = -1;       // what the UI shows (estimated while on USB)
    bool charging_ = false;
    bool usbPresent_ = false;
    uint32_t mvSum_ = 0;
    int samples_ = 0;
    int onBatteryCount_ = 0;    // consecutive on-battery reads (debounce)
    uint32_t usbSinceMs_ = 0;
    int pctAtUsb_ = -1;         // real % when USB was connected

    void readNow();
    void savePercent();
    static int percentFromMv(uint32_t mv);
};

#else
// SPI DevKitC board has no battery monitoring.
class Battery {
public:
    void begin() {}
    void update(uint32_t) {}
    int percent() const { return -1; }
    uint16_t millivolts() const { return 0; }
    bool charging() const { return false; }
    bool usbPresent() const { return false; }
};
#endif
