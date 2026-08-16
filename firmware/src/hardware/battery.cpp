#include "battery.h"

#include <Preferences.h>

#if defined(DISPLAY_8080)

void Battery::begin() {
    analogSetPinAttenuation(kAdcPin, ADC_11db);
    Preferences pref;
    pref.begin("codepet", true);
    realPercent_ = pref.getInt("bat_pct", -1);   // survive reboots while on USB
    pref.end();
    displayPct_ = realPercent_;
    readNow();
}

void Battery::update(uint32_t nowMs) {
    if (nowMs - lastReadMs_ < 2000) return;
    lastReadMs_ = nowMs;
    readNow();
}

void Battery::readNow() {
    // The 100K/100K divider sits on the USB-OR'd power rail (VBUS through
    // D3), so it reports the USB rail (~4.8V) while USB is connected and the
    // actual battery voltage (3.0..4.2V) only on battery power.
    const uint32_t v = analogReadMilliVolts(kAdcPin) * 2;

    mvSum_ += v;
    samples_++;
    if (samples_ < 4) return;   // average 4 samples
    mv_ = (uint16_t)(mvSum_ / samples_);
    mvSum_ = 0;
    samples_ = 0;

    const bool usb = (mv_ > 4300);   // above the LiPo ceiling => USB rail

    if (usb && !usbPresent_) {
        // USB just connected: start the charge-time estimate from the last
        // known real percentage.
        usbSinceMs_ = millis();
        pctAtUsb_ = realPercent_;
        onBatteryCount_ = 0;
    }
    usbPresent_ = usb;

    if (usb) {
        // Cell voltage is hidden on USB; estimate the % from charge time.
        const int base = (pctAtUsb_ >= 0) ? pctAtUsb_ : 50;
        const uint32_t elapsedS = (millis() - usbSinceMs_) / 1000;
        displayPct_ = base + (int)(elapsedS / kSecPerPct);
        if (displayPct_ > 100) displayPct_ = 100;
        charging_ = displayPct_ < 100;   // stop when the estimate reaches 100%
    } else {
        if (++onBatteryCount_ >= 2) {
            // Two consecutive on-battery reads: debounce the plug/unplug
            // transient, then take the real reading.
            realPercent_ = percentFromMv(mv_);
            displayPct_ = realPercent_;
            savePercent();
        }
        charging_ = false;
    }
    Serial.printf("[BAT] v=%umV usb=%d chg=%d pct=%d\n",
                  mv_, (int)usb, (int)charging_, displayPct_);
}

void Battery::savePercent() {
    Preferences pref;
    pref.begin("codepet", false);
    pref.putInt("bat_pct", realPercent_);
    pref.end();
}

int Battery::percentFromMv(uint32_t mv) {
    // Piecewise LiPo curve (3.3V..4.2V usable range for a small cell).
    if (mv >= 4200) return 100;
    if (mv >= 3900) return 50 + (mv - 3900) * 50 / 300;
    if (mv >= 3600) return 10 + (mv - 3600) * 40 / 300;
    if (mv >= 3300) return (mv - 3300) * 10 / 300;
    return 0;
}

#endif
