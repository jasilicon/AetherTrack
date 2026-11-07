#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct sqlite3;

struct Skill
{
    int64_t id{};
    std::string name;
    std::string color_hex;
    bool is_running{};
    int64_t last_start_epoch{};
    int64_t banked_seconds{};
    int64_t lifetime_reset_seconds{};
};

struct HistoryEntry
{
    int64_t id{};
    int64_t skill_id{};
    int64_t duration_seconds{};
    int64_t ended_at_epoch{};
    std::string note;
};

class DatabaseError : public std::runtime_error
{
public:
    explicit DatabaseError(std::string message, int sqlite_code = 0)
        : std::runtime_error(std::move(message))
        , code_(sqlite_code)
    {
    }

    [[nodiscard]] int code() const noexcept { return code_; }

private:
    int code_;
};

class Database
{
public:
    Database();
    explicit Database(std::string database_path);
    ~Database();

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Writes only happen here, and only when the UI calls these methods in
    // response to an explicit user action (Start, Pause, Trim, Reset).
    // There is deliberately no per-tick persistence method.
    [[nodiscard]] int64_t createSkill(std::string_view name, std::string_view color_hex);
    void updateLiveState(int64_t skill_id,
                         bool is_running,
                         int64_t last_start_epoch,
                         int64_t banked_seconds);
    void resetAndLogSession(int64_t skill_id,
                            int64_t duration_seconds,
                            int64_t ended_at_epoch,
                            std::string_view note = {});

    // Read-only queries. Safe to call on every UI refresh; they perform no writes.
    [[nodiscard]] std::vector<Skill> getAllSkills() const;
    [[nodiscard]] std::vector<HistoryEntry> getRecentHistory(int64_t skill_id,
                                                             int limit = 90) const;
    [[nodiscard]] std::vector<HistoryEntry> getHistorySince(int64_t skill_id,
                                                            int64_t since_epoch) const;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] static std::string default_database_path();

private:
    void initialize_schema();
    void exec(std::string_view sql) const;

    sqlite3* db_{nullptr};
    std::string path_;
};
