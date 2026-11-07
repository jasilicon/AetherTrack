#include "core/TimerEngine.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
int64_t saturatingAdd(int64_t lhs, int64_t rhs)
{
    if (rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) {
        return std::numeric_limits<int64_t>::max();
    }
    if (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs) {
        return std::numeric_limits<int64_t>::min();
    }
    return lhs + rhs;
}
} // namespace

TimerEngine::TimerEngine(Database& database, int64_t skill_id)
    : database_(database)
{
    const auto skills = database_.getAllSkills();
    const auto it = std::find_if(
        skills.begin(),
        skills.end(),
        [skill_id](const Skill& skill) { return skill.id == skill_id; });

    if (it == skills.end()) {
        throw std::invalid_argument("TimerEngine: skill id not found");
    }

    skill_ = *it;
}

void TimerEngine::start()
{
    if (skill_.is_running) {
        return;
    }

    const int64_t now = nowEpochSeconds();
    database_.updateLiveState(skill_.id, true, now, skill_.banked_seconds);

    skill_.is_running = true;
    skill_.last_start_epoch = now;
}

void TimerEngine::pause()
{
    if (!skill_.is_running) {
        return;
    }

    const int64_t now = nowEpochSeconds();
    const int64_t elapsed = currentRunSecondsAt(now);
    database_.updateLiveState(skill_.id, false, 0, elapsed);

    skill_.is_running = false;
    skill_.last_start_epoch = 0;
    skill_.banked_seconds = elapsed;
}

void TimerEngine::trimSeconds(int64_t seconds_to_remove)
{
    if (seconds_to_remove < 0) {
        throw std::invalid_argument("TimerEngine::trimSeconds: seconds_to_remove must be non-negative");
    }
    if (seconds_to_remove == 0) {
        return;
    }

    const int64_t now = nowEpochSeconds();
    const int64_t total = currentRunSecondsAt(now);
    const int64_t remaining = std::max<int64_t>(0, total - seconds_to_remove);

    if (skill_.is_running) {
        // Rebase the live segment so future ticks continue from the trimmed
        // total without losing the running state.
        database_.updateLiveState(skill_.id, true, now, remaining);
        skill_.banked_seconds = remaining;
        skill_.last_start_epoch = now;
    } else {
        database_.updateLiveState(skill_.id, false, 0, remaining);
        skill_.banked_seconds = remaining;
    }
}

void TimerEngine::resetToHistory(const std::string& note)
{
    const int64_t now = nowEpochSeconds();
    const int64_t total = currentRunSecondsAt(now);

    database_.resetAndLogSession(skill_.id, total, now, note);

    skill_.is_running = false;
    skill_.last_start_epoch = 0;
    skill_.banked_seconds = 0;
    skill_.lifetime_reset_seconds = saturatingAdd(skill_.lifetime_reset_seconds, total);
}

int64_t TimerEngine::currentRunSeconds() const
{
    return currentRunSecondsAt(nowEpochSeconds());
}

int64_t TimerEngine::currentRunSecondsAt(int64_t now_epoch) const
{
    int64_t total = skill_.banked_seconds;

    if (skill_.is_running && now_epoch > skill_.last_start_epoch) {
        total = saturatingAdd(total, now_epoch - skill_.last_start_epoch);
    }

    return std::max<int64_t>(0, total);
}

int64_t TimerEngine::nowEpochSeconds()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::string TimerEngine::formatDuration(int64_t total_seconds)
{
    if (total_seconds < 0) {
        total_seconds = 0;
    }

    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = (total_seconds / 60) % 60;
    const int64_t seconds = total_seconds % 60;

    int digits = 1;
    for (int64_t value = hours; value >= 10; value /= 10) {
        ++digits;
    }
    const int hours_width = std::max(2, digits);

    std::ostringstream formatted;
    formatted << std::setfill('0')
              << std::setw(hours_width) << hours << ':'
              << std::setw(2) << minutes << ':'
              << std::setw(2) << seconds;
    return formatted.str();
}
