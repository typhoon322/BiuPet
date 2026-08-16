#pragma once

#include <Arduino.h>

// Two tactile buttons on the T-Display-S3 (from the official pin table):
//   Button 1 = GPIO0  (BOOT), Button 2 = GPIO14.  Pressed = LOW.
// Short press (<1s), long press (>0.8s) and very-long press (>3s, B1 = power
// off) are reported.
enum class PetButton {
    NONE = 0,
    B1_SHORT,       // Button 1 short press  -> cycle pages
    B1_LONG,        // Button 1 held         -> cycle pet state (demo)
    B1_VERY_LONG,   // Button 1 held >3s     -> power off (deep sleep)
    B2_SHORT,       // Button 2 short press  -> toggle backlight
    B2_LONG,        // Button 2 held         -> brightness step
};

#if defined(DISPLAY_8080)

class Buttons {
public:
    void begin();
    void reinit();   // re-sample pins (after waking) so a held button doesn't fire
    PetButton update(uint32_t nowMs);

private:
    struct State {
        uint8_t stable = 1;      // 1 = released (HIGH), 0 = pressed
        uint8_t lastRaw = 1;
        uint8_t count = 0;
        uint32_t downAt = 0;
        bool longFired = false;
        bool veryLongFired = false;
    };
    State s1_, s2_;

    static PetButton scan(State& s, int pin, uint32_t now,
                          PetButton shortEv, PetButton longEv, PetButton veryLongEv);
};

#else
// SPI DevKitC board: no buttons.
class Buttons {
public:
    void begin() {}
    void reinit() {}
    PetButton update(uint32_t) { return PetButton::NONE; }
};
#endif
