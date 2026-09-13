#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace auction::database
{
// Statements own their prepared SQL. Keep the parent Connection alive until they are destroyed.
class Statement final
{
  public:
    Statement(sqlite3* connection, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int index, std::int64_t value);
    void bind(int index, const std::string& value);
    void bindNull(int index);
    bool step();
    void run();
    void reset();
    std::int64_t integer(int column) const;
    std::string text(int column) const;

  private:
    sqlite3_stmt* statement_ = nullptr;
};

// All application SQLite connections must use this factory/RAII wrapper.
// Each connection enables and verifies foreign keys before it can be used.
class Connection final
{
  public:
    explicit Connection(const std::filesystem::path& path);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Statement prepare(const std::string& sql);
    void execute(const std::string& sql);
    void executeScript(const std::string& sql);
    std::int64_t scalar(const std::string& sql);

  private:
    struct Close { void operator()(sqlite3* connection) const; };
    std::unique_ptr<sqlite3, Close> connection_;
};

// Uses a versioned, atomic schema + seed transaction. Existing databases are not reseeded.
void initialize(Connection& connection, const std::filesystem::path& dataDirectory);
}
