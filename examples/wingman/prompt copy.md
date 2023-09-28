Kindly check the following code for correctness and completeness, and then implement the TODOs.

Please keep in mind these minimal requirements:

- No use of `SELECT *` in the code. Use the `getTableInfo` function to get the table info, and dynamically construct the `SELECT` statement.
- Write an individual function to read, and another to write, the data for each table.
- The `created` and `updated` columns should be set to the current time in milliseconds since epoch.
- Make use of existing functions where possible.

```cpp
#include <iostream>
#include <string>
#include <map>
#include <stdexcept>
#include <optional>
#include <sqlite3.h>
#include <json.hpp>

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

struct AppItem {
    std::string name;
    std::string key;
    std::string value;
    int enabled;
    long long created;
    long long updated;
};

enum class DownloadItemStatus {
    idle,
    queued,
    downloading,
    complete,
    error,
    cancelled,
    unknown
};

// write a function to return a string representation of the enum
std::string downloadItemStatusToString(DownloadItemStatus status)
{
    switch (status) {
    case DownloadItemStatus::idle:
        return "idle";
    case DownloadItemStatus::queued:
        return "queued";
    case DownloadItemStatus::downloading:
        return "downloading";
    case DownloadItemStatus::complete:
        return "complete";
    case DownloadItemStatus::error:
        return "error";
    case DownloadItemStatus::cancelled:
        return "cancelled";
    case DownloadItemStatus::unknown:
        return "unknown";
    }
}

DownloadItemStatus stringToDownloadItemStatus(const std::string& status)
{
    if (status == "idle") {
        return DownloadItemStatus::idle;
    } else if (status == "queued") {
        return DownloadItemStatus::queued;
    } else if (status == "downloading") {
        return DownloadItemStatus::downloading;
    } else if (status == "complete") {
        return DownloadItemStatus::complete;
    } else if (status == "error") {
        return DownloadItemStatus::error;
    } else if (status == "cancelled") {
        return DownloadItemStatus::cancelled;
    } else if (status == "unknown") {
        return DownloadItemStatus::unknown;
    } else {
        return DownloadItemStatus::idle;
    }
}

DownloadItemStatus stringToDownloadItemStatus(const unsigned char* input)
{
    std::string status(reinterpret_cast<const char*>(input));
    return stringToDownloadItemStatus(status);
}

struct DownloadItemName {
    std::string modelRepo;
    std::string filePath;
};

struct DownloadItem {
    std::string modelRepo;
    std::string filePath;
    // possible values for status are:
    // - idle - download is available to be queued
    // - queued - download is queued, and next in line to be downloaded
    // - downloading - download is in progress
    // - complete - download is complete
    // - error - download failed, and will not be considered until it is reset to idle
    // - cancelled - download was cancelled, and will be deleted
    // - unknown - download is in an unknown state and will be deleted at next startup
    DownloadItemStatus status;
    int totalBytes;
    int downloadedBytes;
    std::string downloadSpeed;
    double progress;
    std::string error;
    long long created;
    long long updated;
};

enum class WingmanItemStatus {
    idle,
    queued,
    inferring,
    complete,
    error,
    cancelling,
    cancelled
};

// write a function to return a string representation of the enum
std::string wingmanItemStatusToString(WingmanItemStatus status)
{
    switch (status) {
    case WingmanItemStatus::idle:
        return "idle";
    case WingmanItemStatus::queued:
        return "queued";
    case WingmanItemStatus::inferring:
        return "inferring";
    case WingmanItemStatus::complete:
        return "complete";
    case WingmanItemStatus::error:
        return "error";
    case WingmanItemStatus::cancelling:
        return "cancelling";
    case WingmanItemStatus::cancelled:
        return "cancelled";
    }
}

WingmanItemStatus stringToWingmanItemStatus(const std::string& status)
{
    if (status == "idle") {
        return WingmanItemStatus::idle;
    } else if (status == "queued") {
        return WingmanItemStatus::queued;
    } else if (status == "inferring") {
        return WingmanItemStatus::inferring;
    } else if (status == "complete") {
        return WingmanItemStatus::complete;
    } else if (status == "error") {
        return WingmanItemStatus::error;
    } else if (status == "cancelling") {
        return WingmanItemStatus::cancelling;
    } else if (status == "cancelled") {
        return WingmanItemStatus::cancelled;
    } else {
        return WingmanItemStatus::idle;
    }
}

WingmanItemStatus stringToWingmanItemStatus(const unsigned char* input)
{
    std::string status(reinterpret_cast<const char*>(input));
    return stringToWingmanItemStatus(status);
}

struct WingmanItem {
    std::string alias;
    // possible values for status are:
    // idle - model instance is available to be queued
    // queued - model instance is queued, and next in line to be loaded into memory and run
    // inferring - model instance is inferring
    // complete - inference is complete and will be removed from memory
    // error - inference failed, and will not be considered until it is reset to idle
    // cancelling - inference is being cancelled
    // cancelled - inference was cancelled, and will be deleted
    WingmanItemStatus status;
    std::string modelRepo;
    std::string filePath;
    int force;
    std::string error;
    long long created;
    long long updated;
};

sqlite3* openDatabase(const std::string& dbPath)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("(openDatabase) Failed to open database: " + std::string(sqlite3_errmsg(db)));
    }
    return db;
}

std::optional<TableInfo> getTableInfo(sqlite3* db, const std::string& tableName)
{
    if (db == nullptr) {
        throw std::runtime_error("Database not initialized");
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

void createDownloadsTable(sqlite3* db)
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
}

void createWingmanTable(sqlite3* db)
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
}

void createAppTable(sqlite3* db)
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
}

void addSampleData(sqlite3* db) {
    // Sample data for downloads table
    std::string sqlDownloads = "INSERT INTO downloads (modelRepo, filePath, status, totalBytes, downloadedBytes, progress, created, updated) "
                               "VALUES ('repo1', 'path1', 'downloading', 1000, 500, 0.5, 1632938400, 1632938400);";
    char* errorMsg = nullptr;
    if (sqlite3_exec(db, sqlDownloads.c_str(), nullptr, nullptr, &errorMsg) != SQLITE_OK) {
        std::string errorString = "Exception in addSampleData (downloads): " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }

    // Sample data for wingman table
    std::string sqlWingman = "INSERT INTO wingman (alias, status, modelRepo, filePath, force, created, updated) "
                             "VALUES ('alias1', 'ready', 'repo1', 'path1', 1, 1632938400, 1632938400);";
    if (sqlite3_exec(db, sqlWingman.c_str(), nullptr, nullptr, &errorMsg) != SQLITE_OK) {
        std::string errorString = "Exception in addSampleData (wingman): " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }

    // Sample data for app table
    std::string sqlApp = "INSERT INTO app (name, key, value, enabled, created, updated) "
                         "VALUES ('appName1', 'key1', 'value1', 1, 1632938400, 1632938400);";
    if (sqlite3_exec(db, sqlApp.c_str(), nullptr, nullptr, &errorMsg) != SQLITE_OK) {
        std::string errorString = "Exception in addSampleData (app): " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
}

int main()
{
    try {
        auto db = openDatabase(":memory:");
        createDownloadsTable(db);
        createWingmanTable(db);
        createAppTable(db);
        addSampleData(db);

        // TODO - call individual functions to read the data from the tables, convert to JSON
        // TODO - convert the JSON to an object of the corresponding struct
        // TODO - print the object
        // TODO - call individual functions to convert the object back to JSON
        // TODO - call individual functions to write the data to the tables
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```
