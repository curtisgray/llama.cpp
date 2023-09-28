#include "common.h"

namespace fs = std::filesystem;

WingmanItemOrm::WingmanItemOrm(sqlite3* dbInstance, std::string& modelsDirectory)
    : db(dbInstance)
    , dbActions(dbInstance)
    , modelsDirectory(modelsDirectory)
{
    fs::create_directories(modelsDirectory);
}

void WingmanItemOrm::upsert(const WingmanItem& item)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(insert) Inserting wingman: " + item.alias);

    std::string sql = "INSERT OR REPLACE INTO wingman (alias, status, modelRepo, filePath, force, error, created, updated) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, item.alias.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, wingmanItemStatusToString(item.status).c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item.modelRepo.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, item.filePath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, item.force);
    sqlite3_bind_text(stmt, 6, item.error.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 7, item.created);
    sqlite3_bind_int64(stmt, 8, item.updated);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(insert) Wingman item inserted.");
}

void WingmanItemOrm::remove(const std::string& alias)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(remove) Removing wingman with alias: " + alias);

    std::string sql = "DELETE FROM wingman WHERE alias = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, alias.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to delete record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(remove) Wingman item removed.");
}

std::optional<WingmanItem> WingmanItemOrm::get(const std::string& alias)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(get) Retrieving wingman with alias: " + alias);

    std::string sql = "SELECT * FROM wingman WHERE alias = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, alias.c_str(), -1, SQLITE_STATIC);

    WingmanItem item;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.alias = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.status = stringToWingmanItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.force = sqlite3_column_int(stmt, 4);
        item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        item.created = sqlite3_column_int64(stmt, 6);
        item.updated = sqlite3_column_int64(stmt, 7);
        sqlite3_finalize(stmt);
        return item;
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(get) Wingman item retrieved.");

    return {};
}

std::vector<WingmanItem> WingmanItemOrm::getAll()
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(getAll) Retrieving all wingman items");

    std::string sql = "SELECT * FROM wingman";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    std::vector<WingmanItem> items;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WingmanItem item;
        item.alias = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.status = stringToWingmanItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.force = sqlite3_column_int(stmt, 4);
        item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        item.created = sqlite3_column_int64(stmt, 6);
        item.updated = sqlite3_column_int64(stmt, 7);
        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(getAll) All wingman items retrieved.");

    return items;
}

void WingmanItemOrm::reset()
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(reset) Resetting wingman table");

    std::string sql = "UPDATE wingman SET status = 'queued' WHERE status = 'inferring' OR status = 'error' or status = 'idle'";

        char* errorMsg
        = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(reset) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }

    sql = "DELETE FROM wingman WHERE status = 'complete' OR status = 'cancelled'";
    errorMsg = nullptr;
    rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(reset) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
    spdlog::debug("(reset) Wingman table reset.");
}
