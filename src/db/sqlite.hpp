#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ote::db {

class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) stmt_ = nullptr;
    }
    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }
    Statement& operator=(Statement&& o) noexcept {
        if (this != &o) {
            if (stmt_) sqlite3_finalize(stmt_);
            stmt_ = o.stmt_;
            o.stmt_ = nullptr;
        }
        return *this;
    }

    bool valid() const { return stmt_ != nullptr; }
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }
    void bind(int i, std::int64_t v) { sqlite3_bind_int64(stmt_, i, v); }
    void bind(int i, int v) { sqlite3_bind_int(stmt_, i, v); }
    void bind(int i, const std::string& v) { sqlite3_bind_text(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT); }
    void bind(int i, const std::vector<std::uint8_t>& v) {
        sqlite3_bind_blob(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    bool exec() {
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_DONE || rc == SQLITE_ROW;
    }
    std::int64_t int64(int c) const { return sqlite3_column_int64(stmt_, c); }
    int integer(int c) const { return sqlite3_column_int(stmt_, c); }
    double real(int c) const { return sqlite3_column_double(stmt_, c); }
    std::string text(int c) const {
        const unsigned char* t = sqlite3_column_text(stmt_, c);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
    }
    std::vector<std::uint8_t> blob(int c) const {
        const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, c));
        int n = sqlite3_column_bytes(stmt_, c);
        return p ? std::vector<std::uint8_t>(p, p + n) : std::vector<std::uint8_t>();
    }
    bool isNull(int c) const { return sqlite3_column_type(stmt_, c) == SQLITE_NULL; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    Database() = default;
    ~Database() { close(); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path, bool readOnly) {
        close();
        int flags = (readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) | SQLITE_OPEN_FULLMUTEX;
        if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
            close();
            return false;
        }
        sqlite3_busy_timeout(db_, 5000);
        return true;
    }
    void close() {
        if (db_) sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    bool isOpen() const { return db_ != nullptr; }
    bool exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
    Statement prepare(const char* sql) { return Statement(db_, sql); }
    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

}
