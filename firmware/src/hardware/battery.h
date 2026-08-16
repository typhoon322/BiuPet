#pragma once

#include <Arduino.h>

// LiPo battery monitoring for the LilyGo T-Display-S3 (320x170).
// GPIO4 = battery voltage via 1/2 divider; charging state is detected
// from the charger status pin (GPIO21, active low) and/or a voltage
// above the charger's ceiling (~4.3V implies USB power is present).
#if defined(DISPLAY_8080)

class Battery {
public:
    void begin();
    void update(uint32_t nowMs);   // sample every ~2s, smooth the ADC

    int percent() const { return percent_; }        // 0..100, -1 = unknown
    uint16_t millivolts() const { return mv_; }
    bool charging() const { return charging_; }
    bool usbPresent() const { return usbPresent_; }
    bool present() const { return present_; }       // false when no battery attached

private:
    static constexpr int kAdcPin = 4;
    static constexpr int kChargePin = 21;
    uint32_t lastReadMs_ = 0;
    uint16_t mv_ = 0;
    int percent_ = -1;
    bool charging_ = false;
    bool usbPresent_ = false;
    bool present_ = false;
    uint32_t mvSum_ = 0;
    int samples_ = 0;
    int onBatteryCount_ = 0;   // consecutive on-battery reads (debounce)

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
    bool present() const { return false; }
};
#endif
