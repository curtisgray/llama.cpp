#include "common.h"
#include "app.h"

void AppOrm::upsert(const AppItem& item)
{
    // Check if ORM is initialized
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    // Log the action
    spdlog::debug("(insert) Inserting app: " + item.name);

    // Prepare the SQL statement for insertion
    std::string sql = "INSERT OR REPLACE INTO app (name, key, value, created, updated) VALUES (?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    // Bind values
    sqlite3_bind_text(stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, item.key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item.value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, item.created);
    sqlite3_bind_int64(stmt, 5, item.updated);

    // Execute statement
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(insert) App item inserted.");
}

void AppOrm::remove(const std::string& name, const std::string& key)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(remove) Removing app with name: " + name + " and key: " + key);

    std::string sql = "DELETE FROM app WHERE name = ? AND key = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to delete record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(remove) App item removed.");
}

std::optional<AppItem> AppOrm::get(const std::string& name, std::optional<std::string> key)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    auto appKey = key.value_or(std::string("default"));

    spdlog::debug(std::format("(get) Retrieving app with name: {0} and key: {1}", name, appKey));

    std::string sql = "SELECT * FROM app WHERE name = ? AND key = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, appKey.c_str(), -1, SQLITE_STATIC);

    AppItem item;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.created = sqlite3_column_int64(stmt, 3);
        item.updated = sqlite3_column_int64(stmt, 4);
        sqlite3_finalize(stmt);
        return item;
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(get) App item retrieved.");

    return {};
}
