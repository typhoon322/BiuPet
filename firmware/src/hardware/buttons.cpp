#include "buttons.h"

#if defined(DISPLAY_8080)

namespace {
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongMs = 800;
constexpr uint32_t kVeryLongMs = 3000;   // B1 held this long => power off
constexpr uint32_t kShortMaxMs = 1000;
}

void Buttons::begin() {
    pinMode(0, INPUT_PULLUP);    // Button 1 (BOOT) - already pulled up externally
    pinMode(14, INPUT_PULLUP);   // Button 2
    reinit();
}

void Buttons::reinit() {
    // Start from the actual pin state so a button held at boot/wake never
    // fires a spurious press.
    s1_.lastRaw = s1_.stable = (digitalRead(0) == LOW) ? 0 : 1;
    s2_.lastRaw = s2_.stable = (digitalRead(14) == LOW) ? 0 : 1;
    if (s1_.stable == 0) { s1_.downAt = millis(); s1_.longFired = true; }
    if (s2_.stable == 0) { s2_.downAt = millis(); s2_.longFired = true; }
}

PetButton Buttons::update(uint32_t nowMs) {
    PetButton e = scan(s1_, 0, nowMs, PetButton::B1_SHORT, PetButton::B1_LONG, PetButton::B1_VERY_LONG);
    if (e != PetButton::NONE) return e;
    return scan(s2_, 14, nowMs, PetButton::B2_SHORT, PetButton::B2_LONG, PetButton::B2_LONG);
}

PetButton Buttons::scan(State& s, int pin, uint32_t now, PetButton shortEv, PetButton longEv, PetButton veryLongEv) {
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
                s.veryLongFired = false;
            } else if (!s.longFired) {
                // released before the long threshold: short press
                s.downAt = 0;
                return shortEv;
            } else {
                s.downAt = 0;   // release after a long/very-long already fired
            }
        } else if (raw == 0) {
            if (!s.longFired && (now - s.downAt) >= kLongMs) {
                s.longFired = true;
                return longEv;
            }
            if (s.longFired && !s.veryLongFired && (now - s.downAt) >= kVeryLongMs) {
                s.veryLongFired = true;
                return veryLongEv;
            }
        }
    }
    return PetButton::NONE;
}

#endif
