#pragma once

#include <cstdint>

enum class PetState : uint8_t {
    OFFLINE = 0,
    IDLE = 1,
    WORKING = 2,
    WAITING = 3,
    COMPLETED = 4,
    ERROR = 5,
    SLEEP = 6
};

inline const char* petStateName(PetState s) {
    switch (s) {
        case PetState::OFFLINE:   return "OFFLINE";
        case PetState::IDLE:      return "IDLE";
        case PetState::WORKING:   return "WORKING";
        case PetState::WAITING:   return "WAITING";
        case PetState::COMPLETED: return "COMPLETED";
        case PetState::ERROR:     return "ERROR";
        case PetState::SLEEP:     return "SLEEP";
    }
    return "UNKNOWN";
}
