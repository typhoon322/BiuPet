#include "pet_stats.h"

void PetStatsStore::begin() {
    load();
}

void PetStatsStore::load() {
    prefs_.begin("pet", true);
    stats_.level = prefs_.getUInt("level", 1);
    stats_.exp = prefs_.getUInt("exp", 0);
    stats_.tasksCompleted = prefs_.getUInt("tasks", 0);
    stats_.workingSeconds = prefs_.getUInt("workSec", 0);
    prefs_.end();
    if (stats_.level < 1) stats_.level = 1;
}

void PetStatsStore::save() {
    prefs_.begin("pet", false);
    prefs_.putUInt("level", stats_.level);
    prefs_.putUInt("exp", stats_.exp);
    prefs_.putUInt("tasks", stats_.tasksCompleted);
    prefs_.putUInt("workSec", stats_.workingSeconds);
    prefs_.end();
}

void PetStatsStore::addExp(uint32_t amount) {
    stats_.exp += amount;
    stats_.level = 1 + stats_.exp / EXP_PER_LEVEL;
    save();
    Serial.printf("[STATS] exp=%u level=%u\n", stats_.exp, stats_.level);
}

void PetStatsStore::onTaskCompleted() {
    stats_.tasksCompleted++;
    addExp(10);
}

void PetStatsStore::onError() {
    addExp(2); // keep small credit; main point is tracking
}

void PetStatsStore::addWorkingSeconds(uint32_t sec) {
    if (sec == 0) return;
    stats_.workingSeconds += sec;
    save();
}
