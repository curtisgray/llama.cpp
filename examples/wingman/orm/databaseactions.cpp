#include "common.h"
#include "databaseactions.h"

DatabaseActions::DatabaseActions(sqlite3* dbInstance)
{
    db = dbInstance;
}

TableInfo DatabaseActions::getTableInfo(const std::string& tableName)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    std::string sql = "PRAGMA table_info(" + tableName + ");";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    TableInfo info;
    info.name = tableName;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TableColumnInfo colInfo;
        colInfo.cid = sqlite3_column_int(stmt, 0);
        colInfo.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        colInfo.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        colInfo.notnull = sqlite3_column_int(stmt, 3);
        colInfo.dflt_value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        colInfo.pk = sqlite3_column_int(stmt, 5);

        info.columns[colInfo.name] = colInfo;
    }

    sqlite3_finalize(stmt);
    return info;
}

void DatabaseActions::createDownloadsTable()
{
    if (db == nullptr) {
        throw std::runtime_error("(createDownloadsTable) Database not initialized");
    }

    std::string sql = "CREATE TABLE IF NOT EXISTS downloads ("
                      "modelRepo TEXT NOT NULL, "
                      "filePath TEXT NOT NULL, "
                      "status TEXT DEFAULT 'idle' NOT NULL, "
                      "totalBytes INTEGER DEFAULT 0 NOT NULL, "
                      "downloadedBytes INTEGER DEFAULT 0 NOT NULL, "
                      "downloadSpeed TEXT, "
                      "progress REAL DEFAULT 0.0 NOT NULL, "
                      "error TEXT, "
                      "created INTEGER DEFAULT 0 NOT NULL, "
                      "updated INTEGER DEFAULT 0 NOT NULL, "
                      "PRIMARY KEY (modelRepo, filePath)"
                      ")";

    char* errorMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(createDownloadsTable) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
    spdlog::debug("(createDownloadsTable) Downloads table created.");
}

void DatabaseActions::createWingmanTable()
{
    if (db == nullptr) {
        throw std::runtime_error("(createWingmanTable)Database not initialized");
    }

    std::string sql = "CREATE TABLE IF NOT EXISTS wingman ("
                      "alias TEXT NOT NULL, "
                      "status TEXT DEFAULT 'idle' NOT NULL, "
                      "modelRepo TEXT NOT NULL, "
                      "filePath TEXT NOT NULL, "
                      "force INTEGER DEFAULT 0 NOT NULL, "
                      "error TEXT, "
                      "created INTEGER DEFAULT 0 NOT NULL, "
                      "updated INTEGER DEFAULT 0 NOT NULL, "
                      "PRIMARY KEY (alias)"
                      ")";

    char* errorMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(createWingmanTable) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
    spdlog::debug("(createWingmanTable) Wingman table created.");
}

void DatabaseActions::createAppTable()
{
    if (db == nullptr) {
        throw std::runtime_error("(createAppTable) Database not initialized");
    }

    std::string sql = "CREATE TABLE IF NOT EXISTS app ("
                      "name TEXT NOT NULL, "
                      "key TEXT NOT NULL, "
                      "value TEXT, "
                      "enabled INTEGER DEFAULT 1 NOT NULL, "
                      "created INTEGER DEFAULT 0 NOT NULL, "
                      "updated INTEGER DEFAULT 0 NOT NULL, "
                      "PRIMARY KEY (name, key)"
                      ")";

    char* errorMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(createAppTable) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
    spdlog::debug("(createAppTable) App table created.");
}
