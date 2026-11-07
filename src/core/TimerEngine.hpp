#pragma once

#include "core/Database.hpp"

#include <cstdint>
#include <string>

// Wall-clock timer state machine.
//
// The engine never runs a background thread and never counts ticks. Elapsed
// time is derived on demand from persisted 64-bit epoch timestamps:
//
//   current = banked_seconds + (is_running ? now_epoch - last_start_epoch : 0)
//
// That makes 10,000+ hour sessions trivial (int64_t handles ~292 billion
// years of seconds) and gives crash recovery for free: a skill loaded with
// is_running == 1 immediately reports the exact wall-clock elapsed time.
class TimerEngine
{
public:
    TimerEngine(Database& database, int64_t skill_id);

    TimerEngine(const TimerEngine&) = delete;
    TimerEngine& operator=(const TimerEngine&) = delete;

    // Explicit user actions. These are the only paths that write to disk.
    void start();
    void pause();
    void trimSeconds(int64_t seconds_to_remove);
    void resetToHistory(const std::string& note);

    [[nodiscard]] int64_t currentRunSeconds() const;
    [[nodiscard]] int64_t currentRunSecondsAt(int64_t now_epoch) const;

    [[nodiscard]] const Skill& skill() const noexcept { return skill_; }
    [[nodiscard]] int64_t skillId() const noexcept { return skill_.id; }
    [[nodiscard]] bool isRunning() const noexcept { return skill_.is_running; }

    [[nodiscard]] static int64_t nowEpochSeconds();
    [[nodiscard]] static std::string formatDuration(int64_t total_seconds);

private:
    Database& database_;
    Skill skill_;
};
