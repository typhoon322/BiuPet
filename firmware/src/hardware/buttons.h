#pragma once

#include <Arduino.h>

// Two tactile buttons on the T-Display-S3 (from the official pin table):
//   Button 1 = GPIO0  (BOOT), Button 2 = GPIO14.  Pressed = LOW.
// Short press (press+release < 1s) and long press (> 800ms) are reported.
enum class PetButton {
    NONE = 0,
    B1_SHORT,   // Button 1 short press  -> info screen
    B1_LONG,    // Button 1 held         -> cycle pet state (demo)
    B2_SHORT,   // Button 2 short press  -> toggle backlight
    B2_LONG,    // Button 2 held         -> (reserved)
};

#if defined(DISPLAY_8080)

class Buttons {
public:
    void begin();
    PetButton update(uint32_t nowMs);

private:
    struct State {
        uint8_t stable = 1;      // 1 = released (HIGH), 0 = pressed
        uint8_t lastRaw = 1;
        uint8_t count = 0;
        uint32_t downAt = 0;
        bool longFired = false;
    };
    State s1_, s2_;

    static PetButton scan(State& s, int pin, uint32_t now, PetButton shortEv, PetButton longEv);
};

#else
// SPI DevKitC board: no buttons.
class Buttons {
public:
    void begin() {}
    PetButton update(uint32_t) { return PetButton::NONE; }
};
#endif
