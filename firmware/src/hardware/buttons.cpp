#include "buttons.h"

#if defined(DISPLAY_8080)

namespace {
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongMs = 800;
constexpr uint32_t kShortMaxMs = 1000;
}

void Buttons::begin() {
    pinMode(0, INPUT_PULLUP);    // Button 1 (BOOT) - already pulled up externally
    pinMode(14, INPUT_PULLUP);   // Button 2
}

PetButton Buttons::update(uint32_t nowMs) {
    PetButton e = scan(s1_, 0, nowMs, PetButton::B1_SHORT, PetButton::B1_LONG);
    if (e != PetButton::NONE) return e;
    return scan(s2_, 14, nowMs, PetButton::B2_SHORT, PetButton::B2_LONG);
}

PetButton Buttons::scan(State& s, int pin, uint32_t now, PetButton shortEv, PetButton longEv) {
    const uint8_t raw = (digitalRead(pin) == LOW) ? 0 : 1;
    if (raw != s.lastRaw) {
        s.lastRaw = raw;
        s.count = 0;
    } else if (++s.count >= (kDebounceMs / 16)) {   // stable for ~40ms (loop runs ~60fps)
        if (s.stable != raw) {
            s.stable = raw;
            if (raw == 0) {
                s.downAt = now;
                s.longFired = false;
            } else if (!s.longFired) {
                // released: short press if it wasn't held long enough for LONG
                s.downAt = 0;
                return shortEv;
            } else {
                s.downAt = 0;
            }
        } else if (raw == 0 && !s.longFired && (now - s.downAt) >= kLongMs) {
            s.longFired = true;
            return longEv;
        }
    }
    return PetButton::NONE;
}

#endif
