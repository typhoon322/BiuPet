#include "battery.h"

#include <Preferences.h>

#if defined(DISPLAY_8080)

void Battery::begin() {
    analogSetPinAttenuation(kAdcPin, ADC_11db);
    Preferences pref;
    pref.begin("codepet", true);
    percent_ = pref.getInt("bat_pct", -1);   // survive reboots while on USB
    pref.end();
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

    // The TP4065 charger exposes no charge-status pin on this board (only the
    // red LED via its PROG pin), and GPIO15 must be HIGH for the charge path to
    // engage (done in setup). So "charging" is inferred: USB power present with
    // chargeable capacity left. Plugging USB in does not imply charging (a full
    // cell is not charged).
    usbPresent_ = usb;
    charging_ = usb && (percent_ != 100);

    if (usb) {
        onBatteryCount_ = 0;         // cell voltage hidden while on USB
    } else if (++onBatteryCount_ >= 2) {
        // Two consecutive on-battery reads: debounce the plug/unplug transient
        // (a brief reading in 4.2..4.3V would otherwise fake a full battery).
        percent_ = percentFromMv(mv_);
        savePercent();
    }
    Serial.printf("[BAT] v=%umV usb=%d chg=%d pct=%d\n",
                  mv_, (int)usb, (int)charging_, percent_);
}

void Battery::savePercent() {
    Preferences pref;
    pref.begin("codepet", false);
    pref.putInt("bat_pct", percent_);
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
