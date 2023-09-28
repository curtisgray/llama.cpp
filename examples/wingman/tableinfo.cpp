#include <iostream>
#include <map>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

struct TableColumnInfo {
    int cid; // column ID
    std::string name; // column name
    std::string type; // column data type
    int notnull; // NOT NULL constraint flag (0 or 1)
    std::string dflt_value; // default value
    int pk; // primary key flag (0 or 1)
};

struct TableInfo {
    std::string name; // table name
    std::map<std::string, TableColumnInfo> columns; // map column name to its info
};

std::optional<TableInfo> getTableInfo(sqlite3* db, const std::string& tableName)
{
    if (db == nullptr) {
        throw std::runtime_error("Database not initialized");
    }

    if (tableName.empty() || !std::all_of(tableName.begin(), tableName.end(), ::isalnum)) {
        throw std::runtime_error("Invalid table name");
    }

    std::string sql = "PRAGMA table_info(" + tableName + ");";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::string errorMsg = sqlite3_errmsg(db);
        throw std::runtime_error("Failed to prepare statement: " + errorMsg);
    }

    TableInfo info;
    info.name = tableName;
    bool hasRows = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        hasRows = true;
        TableColumnInfo colInfo;
        colInfo.cid = sqlite3_column_int(stmt, 0);
        colInfo.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        colInfo.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        colInfo.notnull = sqlite3_column_int(stmt, 3);
        const char* defaultVal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        colInfo.dflt_value = defaultVal ? defaultVal : "";
        colInfo.pk = sqlite3_column_int(stmt, 5);

        info.columns[colInfo.name] = colInfo;
    }

    sqlite3_finalize(stmt);
    if (!hasRows) {
        return {};
    }
    return info;
}

std::optional<TableInfo> getTableInfo(const std::string& databasePath, const std::string& tableName)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath, &db) != SQLITE_OK) {
        std::string errorMsg = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw std::runtime_error("Failed to open the database: " + errorMsg);
    }
    sqlite3_close(db);
    return getTableInfo(db, tableName);
}

int main()
{
    try {
        auto info = getTableInfo(":memory:", "users");
        if (!info) {
            std::cerr << "Failed to get table info." << std::endl;
            return 1;
        }
        std::cout << info->name << std::endl;
        for (auto& [name, colInfo] : info->columns) {
            std::cout << "  " << colInfo.name << " " << colInfo.type << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}