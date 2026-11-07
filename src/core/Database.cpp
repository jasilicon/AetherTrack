#include "core/Database.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace
{
[[noreturn]] void throw_sqlite_error(sqlite3* db, int rc, std::string_view context)
{
    std::string message(context);
    message += ": ";
    message += db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw DatabaseError(std::move(message), rc);
}

void exec_sql(sqlite3* db, std::string_view sql, std::string_view context)
{
    const std::string statement(sql);
    char* error = nullptr;
    const int rc = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, &error);

    if (rc == SQLITE_OK) {
        return;
    }

    std::string message(context);
    message += ": ";
    if (error != nullptr) {
        message += error;
        sqlite3_free(error);
    } else {
        message += sqlite3_errmsg(db);
    }
    throw DatabaseError(std::move(message), rc);
}

class Statement
{
public:
    Statement(sqlite3* db, std::string_view sql)
        : db_(db)
    {
        const int rc = sqlite3_prepare_v2(
            db_,
            sql.data(),
            static_cast<int>(sql.size()),
            &stmt_,
            nullptr);

        if (rc != SQLITE_OK) {
            throw_sqlite_error(db_, rc, "sqlite prepare");
        }
    }

    ~Statement()
    {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(Statement&& other) noexcept
        : db_(other.db_)
        , stmt_(std::exchange(other.stmt_, nullptr))
    {
    }

    Statement& operator=(Statement&& other) noexcept
    {
        if (this != &other) {
            if (stmt_ != nullptr) {
                sqlite3_finalize(stmt_);
            }
            db_ = other.db_;
            stmt_ = std::exchange(other.stmt_, nullptr);
        }
        return *this;
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_int(int index, int value)
    {
        const int rc = sqlite3_bind_int(stmt_, index, value);
        if (rc != SQLITE_OK) {
            throw_sqlite_error(db_, rc, "sqlite bind_int");
        }
    }

    void bind_int64(int index, int64_t value)
    {
        const int rc = sqlite3_bind_int64(stmt_, index, value);
        if (rc != SQLITE_OK) {
            throw_sqlite_error(db_, rc, "sqlite bind_int64");
        }
    }

    void bind_text(int index, std::string_view value)
    {
        const char* data = value.empty() ? "" : value.data();
        const int rc = sqlite3_bind_text(
            stmt_,
            index,
            data,
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT);

        if (rc != SQLITE_OK) {
            throw_sqlite_error(db_, rc, "sqlite bind_text");
        }
    }

    [[nodiscard]] bool step()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw_sqlite_error(db_, rc, "sqlite step");
    }

    void execute()
    {
        if (step()) {
            throw DatabaseError("sqlite execute: statement unexpectedly returned a row");
        }
    }

    [[nodiscard]] int64_t column_int64(int index) const
    {
        return sqlite3_column_int64(stmt_, index);
    }

    [[nodiscard]] int column_int(int index) const
    {
        return sqlite3_column_int(stmt_, index);
    }

    [[nodiscard]] std::string column_text(int index) const
    {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
        if (text == nullptr) {
            return {};
        }
        return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
    }

    [[nodiscard]] int64_t last_insert_rowid() const
    {
        return sqlite3_last_insert_rowid(db_);
    }

private:
    sqlite3* db_{nullptr};
    sqlite3_stmt* stmt_{nullptr};
};

class Transaction
{
public:
    explicit Transaction(sqlite3* db)
        : db_(db)
    {
        exec_sql(db_, "BEGIN IMMEDIATE;", "sqlite begin transaction");
    }

    ~Transaction()
    {
        if (!committed_ && db_ != nullptr) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit()
    {
        exec_sql(db_, "COMMIT;", "sqlite commit transaction");
        committed_ = true;
    }

private:
    sqlite3* db_{nullptr};
    bool committed_{false};
};

constexpr std::string_view kSchema = R"sql(
CREATE TABLE IF NOT EXISTS skills (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    color_hex TEXT NOT NULL DEFAULT '#60A5FA',
    is_running INTEGER NOT NULL DEFAULT 0 CHECK (is_running IN (0, 1)),
    last_start_epoch INTEGER NOT NULL DEFAULT 0,
    banked_seconds INTEGER NOT NULL DEFAULT 0,
    lifetime_Reset_seconds INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS history_logs (
    id INTEGER PRIMARY KEY,
    skill_id INTEGER NOT NULL REFERENCES skills(id) ON DELETE CASCADE,
    duration_seconds INTEGER NOT NULL DEFAULT 0,
    ended_at_epoch INTEGER NOT NULL DEFAULT 0,
    note TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_history_logs_skill_ended
    ON history_logs(skill_id, ended_at_epoch DESC);

PRAGMA user_version = 1;
)sql";
} // namespace

Database::Database()
    : Database(default_database_path())
{
}

Database::Database(std::string database_path)
    : path_(std::move(database_path))
{
    if (path_.empty()) {
        throw DatabaseError("database path must not be empty");
    }

    const std::filesystem::path file_path(path_);
    if (file_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(file_path.parent_path(), error);
        if (error) {
            throw DatabaseError(
                "cannot create database directory '" + file_path.parent_path().string()
                + "': " + error.message());
        }
    }

    const int rc = sqlite3_open_v2(
        path_.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (rc != SQLITE_OK) {
        std::string message = "cannot open database '" + path_ + "': ";
        message += db_ != nullptr ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        throw DatabaseError(std::move(message), rc);
    }

    try {
        sqlite3_busy_timeout(db_, 5000);
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA synchronous = NORMAL;");
        initialize_schema();
    } catch (...) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw;
    }
}

Database::~Database()
{
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

Database::Database(Database&& other) noexcept
    : db_(std::exchange(other.db_, nullptr))
    , path_(std::move(other.path_))
{
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
        }
        db_ = std::exchange(other.db_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

int64_t Database::createSkill(std::string_view name, std::string_view color_hex)
{
    if (name.empty()) {
        throw std::invalid_argument("createSkill: name must not be empty");
    }

    Statement statement(
        db_,
        "INSERT INTO skills (name, color_hex) VALUES (?, ?);");
    statement.bind_text(1, name);
    statement.bind_text(2, color_hex.empty() ? std::string_view{"#60A5FA"} : color_hex);
    statement.execute();

    return statement.last_insert_rowid();
}

std::vector<Skill> Database::getAllSkills() const
{
    Statement statement(
        db_,
        "SELECT id, name, color_hex, is_running, last_start_epoch, "
        "banked_seconds, lifetime_Reset_seconds "
        "FROM skills ORDER BY id ASC;");

    std::vector<Skill> skills;
    while (statement.step()) {
        Skill skill;
        skill.id = statement.column_int64(0);
        skill.name = statement.column_text(1);
        skill.color_hex = statement.column_text(2);
        skill.is_running = statement.column_int(3) != 0;
        skill.last_start_epoch = statement.column_int64(4);
        skill.banked_seconds = statement.column_int64(5);
        skill.lifetime_reset_seconds = statement.column_int64(6);
        skills.push_back(std::move(skill));
    }
    return skills;
}

void Database::updateLiveState(int64_t skill_id,
                               bool is_running,
                               int64_t last_start_epoch,
                               int64_t banked_seconds)
{
    if (banked_seconds < 0) {
        throw std::invalid_argument("updateLiveState: banked_seconds must be non-negative");
    }

    Statement statement(
        db_,
        "UPDATE skills "
        "SET is_running = ?, last_start_epoch = ?, banked_seconds = ? "
        "WHERE id = ?;");
    statement.bind_int(1, is_running ? 1 : 0);
    statement.bind_int64(2, last_start_epoch);
    statement.bind_int64(3, banked_seconds);
    statement.bind_int64(4, skill_id);
    statement.execute();

    if (sqlite3_changes(db_) == 0) {
        throw DatabaseError("updateLiveState: skill not found");
    }
}

void Database::resetAndLogSession(int64_t skill_id,
                                  int64_t duration_seconds,
                                  int64_t ended_at_epoch,
                                  std::string_view note)
{
    if (duration_seconds < 0) {
        throw std::invalid_argument("resetAndLogSession: duration_seconds must be non-negative");
    }

    Transaction transaction(db_);

    {
        Statement log(
            db_,
            "INSERT INTO history_logs "
            "(skill_id, duration_seconds, ended_at_epoch, note) "
            "VALUES (?, ?, ?, ?);");
        log.bind_int64(1, skill_id);
        log.bind_int64(2, duration_seconds);
        log.bind_int64(3, ended_at_epoch);
        log.bind_text(4, note);
        log.execute();
    }

    {
        Statement reset(
            db_,
            "UPDATE skills "
            "SET is_running = 0, "
            "    last_start_epoch = 0, "
            "    banked_seconds = 0, "
            "    lifetime_Reset_seconds = lifetime_Reset_seconds + ? "
            "WHERE id = ?;");
        reset.bind_int64(1, duration_seconds);
        reset.bind_int64(2, skill_id);
        reset.execute();

        if (sqlite3_changes(db_) == 0) {
            throw DatabaseError("resetAndLogSession: skill not found");
        }
    }

    transaction.commit();
}

std::vector<HistoryEntry> Database::getRecentHistory(int64_t skill_id, int limit) const
{
    if (limit <= 0) {
        return {};
    }

    Statement statement(
        db_,
        "SELECT id, skill_id, duration_seconds, ended_at_epoch, note "
        "FROM history_logs "
        "WHERE skill_id = ? "
        "ORDER BY ended_at_epoch DESC, id DESC "
        "LIMIT ?;");
    statement.bind_int64(1, skill_id);
    statement.bind_int(2, limit);

    std::vector<HistoryEntry> history;
    while (statement.step()) {
        HistoryEntry entry;
        entry.id = statement.column_int64(0);
        entry.skill_id = statement.column_int64(1);
        entry.duration_seconds = statement.column_int64(2);
        entry.ended_at_epoch = statement.column_int64(3);
        entry.note = statement.column_text(4);
        history.push_back(std::move(entry));
    }
    return history;
}

std::vector<HistoryEntry> Database::getHistorySince(int64_t skill_id,
                                                     int64_t since_epoch) const
{
    Statement statement(
        db_,
        "SELECT id, skill_id, duration_seconds, ended_at_epoch, note "
        "FROM history_logs "
        "WHERE skill_id = ? AND ended_at_epoch >= ? "
        "ORDER BY ended_at_epoch DESC, id DESC;");
    statement.bind_int64(1, skill_id);
    statement.bind_int64(2, since_epoch);

    std::vector<HistoryEntry> history;
    while (statement.step()) {
        HistoryEntry entry;
        entry.id = statement.column_int64(0);
        entry.skill_id = statement.column_int64(1);
        entry.duration_seconds = statement.column_int64(2);
        entry.ended_at_epoch = statement.column_int64(3);
        entry.note = statement.column_text(4);
        history.push_back(std::move(entry));
    }
    return history;
}

std::string Database::default_database_path()
{
    std::filesystem::path base;

    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home != nullptr && *xdg_data_home != '\0') {
        const std::filesystem::path candidate(xdg_data_home);
        if (candidate.is_absolute()) {
            base = candidate;
        }
    }

    if (base.empty()) {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            throw DatabaseError(
                "cannot resolve XDG data directory: HOME is unset");
        }

        const std::filesystem::path home_path(home);
        if (!home_path.is_absolute()) {
            throw DatabaseError("cannot resolve XDG data directory: HOME must be absolute");
        }

        base = home_path / ".local" / "share";
    }

    return (base / "AetherTrack" / "AetherTrack.db").string();
}

void Database::initialize_schema()
{
    exec(kSchema);
}

void Database::exec(std::string_view sql) const
{
    exec_sql(db_, sql, "sqlite exec");
}
