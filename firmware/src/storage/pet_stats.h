#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct PetStats {
    uint32_t level = 1;
    uint32_t exp = 0;               // total exp
    uint32_t tasksCompleted = 0;
    uint32_t workingSeconds = 0;
};

class PetStatsStore {
public:
    void begin();
    void addExp(uint32_t amount);
    void onTaskCompleted();          // +10 exp, tasksCompleted++
    void onError();                  // small penalty on next-task exp? kept simple: +2 exp but tracked
    void addWorkingSeconds(uint32_t sec);
    void flush();                    // persist any pending workingSeconds
    const PetStats& stats() const { return stats_; }
    uint32_t expInLevel() const { return stats_.exp % EXP_PER_LEVEL; }
    uint32_t expNeeded() const { return EXP_PER_LEVEL - expInLevel(); }
    static constexpr uint32_t EXP_PER_LEVEL = 100;
    // workingSeconds accumulates every second; don't wear NVS with 1 write/sec
    static constexpr uint32_t SAVE_INTERVAL_MS = 60000;

private:
    Preferences prefs_;
    PetStats stats_;
    uint32_t lastSaveMs_ = 0;
    bool dirty_ = false;
    void load();
    void save();
};
