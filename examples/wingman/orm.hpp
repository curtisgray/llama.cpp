class AppItemActions {
private:
    sqlite3* db;
    std::map<std::string, int> columnIndices;

    void initializeColumnIndices()
    {
        std::string sql = "PRAGMA table_info(app)";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(initializeColumnIndices) Unable to prepare statement");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string columnName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int columnIndex = sqlite3_column_int(stmt, 0);
            columnIndices[columnName] = columnIndex;
        }

        sqlite3_finalize(stmt);
    }

public:
    AppItemActions(sqlite3* db)
        : db(db)
    {
        initializeColumnIndices();
    }

    std::optional<AppItem> read(const std::string& name, const std::string& key)
    {
        std::string sql = "SELECT name, key, value, enabled, created, updated FROM app WHERE name = ? AND key = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(read) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            AppItem item;
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["name"]));
            item.key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["key"]));
            item.value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["value"]));
            item.enabled = sqlite3_column_int(stmt, columnIndices["enabled"]);
            item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
            item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);

            sqlite3_finalize(stmt);
            return item;
        }

        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    void update(const AppItem& item)
    {
        std::string sql = "UPDATE app SET value = ?, enabled = ?, updated = ? WHERE name = ? AND key = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(update) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, item.value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, item.enabled);
        sqlite3_bind_int64(stmt, 3, item.updated);
        sqlite3_bind_text(stmt, 4, item.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, item.key.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(update) Failed to update record");
        }

        sqlite3_finalize(stmt);
    }

    void remove(const std::string& name, const std::string& key)
    {
        std::string sql = "DELETE FROM app WHERE name = ? AND key = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(remove) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(remove) Failed to delete record");
        }

        sqlite3_finalize(stmt);
    }

    nlohmann::json toJson(const AppItem& item)
    {
        nlohmann::json j;
        j["name"] = item.name;
        j["key"] = item.key;
        j["value"] = item.value;
        j["enabled"] = item.enabled;
        j["created"] = item.created;
        j["updated"] = item.updated;

        return j;
    }

    AppItem fromJson(const nlohmann::json& j)
    {
        AppItem item;
        item.name = j["name"];
        item.key = j["key"];
        item.value = j["value"];
        item.enabled = j["enabled"];
        item.created = j["created"];
        item.updated = j["updated"];

        return item;
    }
};

class DownloadItemActions {
private:
    sqlite3* db;
    std::map<std::string, int> columnIndices;

    void initializeColumnIndices()
    {
        std::string sql = "PRAGMA table_info(downloads)";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(initializeColumnIndices) Unable to prepare statement");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string columnName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int columnIndex = sqlite3_column_int(stmt, 0);
            columnIndices[columnName] = columnIndex;
        }

        sqlite3_finalize(stmt);
    }

public:
    DownloadItemActions(sqlite3* db)
        : db(db)
    {
        initializeColumnIndices();
    }

    std::optional<DownloadItem> read(const std::string& modelRepo, const std::string& filePath)
    {
        std::string sql = "SELECT modelRepo, filePath, status, totalBytes, downloadedBytes, downloadSpeed, progress, error, created, updated "
                          "FROM downloads WHERE modelRepo = ? AND filePath = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(read) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, modelRepo.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, filePath.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            DownloadItem item;
            item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["modelRepo"]));
            item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["filePath"]));
            item.status = stringToDownloadItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["status"])));
            item.totalBytes = sqlite3_column_int(stmt, columnIndices["totalBytes"]);
            item.downloadedBytes = sqlite3_column_int(stmt, columnIndices["downloadedBytes"]);
            item.downloadSpeed = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["downloadSpeed"]));
            item.progress = sqlite3_column_double(stmt, columnIndices["progress"]);
            item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["error"]));
            item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
            item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);

            sqlite3_finalize(stmt);
            return item;
        }

        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    void update(const DownloadItem& item)
    {
        std::string sql = "UPDATE downloads SET status = ?, totalBytes = ?, downloadedBytes = ?, downloadSpeed = ?, progress = ?, "
                          "error = ?, updated = ? WHERE modelRepo = ? AND filePath = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(update) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, downloadItemStatusToString(item.status).c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, item.totalBytes);
        sqlite3_bind_int(stmt, 3, item.downloadedBytes);
        sqlite3_bind_text(stmt, 4, item.downloadSpeed.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 5, item.progress);
        sqlite3_bind_text(stmt, 6, item.error.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 7, item.updated);
        sqlite3_bind_text(stmt, 8, item.modelRepo.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, item.filePath.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(update) Failed to update record");
        }

        sqlite3_finalize(stmt);
    }

    void remove(const std::string& modelRepo, const std::string& filePath)
    {
        std::string sql = "DELETE FROM downloads WHERE modelRepo = ? AND filePath = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(remove) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, modelRepo.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, filePath.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(remove) Failed to delete record");
        }

        sqlite3_finalize(stmt);
    }

    std::vector<DownloadItem> getAllByStatus(DownloadItemStatus status)
    {
        std::vector<DownloadItem> items;
        std::string sql = "SELECT modelRepo, filePath, status, totalBytes, downloadedBytes, downloadSpeed, progress, error, created, updated "
                          "FROM downloads WHERE status = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(getAllByStatus) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, downloadItemStatusToString(status).c_str(), -1, SQLITE_STATIC);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DownloadItem item;
            item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["modelRepo"]));
            item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["filePath"]));
            item.status = stringToDownloadItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["status"])));
            item.totalBytes = sqlite3_column_int(stmt, columnIndices["totalBytes"]);
            item.downloadedBytes = sqlite3_column_int(stmt, columnIndices["downloadedBytes"]);
            item.downloadSpeed = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["downloadSpeed"]));
            item.progress = sqlite3_column_double(stmt, columnIndices["progress"]);
            item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["error"]));
            item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
            item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);
            items.push_back(item);
        }

        sqlite3_finalize(stmt);
        return items;
    }

    nlohmann::json toJson(const DownloadItem& item)
    {
        nlohmann::json j;
        j["modelRepo"] = item.modelRepo;
        j["filePath"] = item.filePath;
        j["status"] = downloadItemStatusToString(item.status);
        j["totalBytes"] = item.totalBytes;
        j["downloadedBytes"] = item.downloadedBytes;
        j["downloadSpeed"] = item.downloadSpeed;
        j["progress"] = item.progress;
        j["error"] = item.error;
        j["created"] = item.created;
        j["updated"] = item.updated;

        return j;
    }

    DownloadItem fromJson(const nlohmann::json& j)
    {
        DownloadItem item;
        item.modelRepo = j["modelRepo"];
        item.filePath = j["filePath"];
        item.status = stringToDownloadItemStatus(j["status"]);
        item.totalBytes = j["totalBytes"];
        item.downloadedBytes = j["downloadedBytes"];
        item.downloadSpeed = j["downloadSpeed"];
        item.progress = j["progress"];
        item.error = j["error"];
        item.created = j["created"];
        item.updated = j["updated"];

        return item;
    }
};

class WingmanItemActions {
private:
    sqlite3* db;
    std::map<std::string, int> columnIndices;

    void initializeColumnIndices()
    {
        std::string sql = "PRAGMA table_info(wingman)";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(initializeColumnIndices) Unable to prepare statement");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string columnName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int columnIndex = sqlite3_column_int(stmt, 0);
            columnIndices[columnName] = columnIndex;
        }

        sqlite3_finalize(stmt);
    }

public:
    WingmanItemActions(sqlite3* db)
        : db(db)
    {
        initializeColumnIndices();
    }

    std::optional<WingmanItem> read(const std::string& alias)
    {
        std::string sql = "SELECT alias, status, modelRepo, filePath, force, error, created, updated "
                          "FROM wingman WHERE alias = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(read) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, alias.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            WingmanItem item;
            item.alias = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["alias"]));
            item.status = stringToWingmanItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["status"])));
            item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["modelRepo"]));
            item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["filePath"]));
            item.force = sqlite3_column_int(stmt, columnIndices["force"]);
            item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, columnIndices["error"]));
            item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
            item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);

            sqlite3_finalize(stmt);
            return item;
        }

        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    void update(const WingmanItem& item)
    {
        std::string sql = "UPDATE wingman SET status = ?, modelRepo = ?, filePath = ?, force = ?, error = ?, updated = ? "
                          "WHERE alias = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(update) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, wingmanItemStatusToString(item.status).c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, item.modelRepo.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, item.filePath.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, item.force);
        sqlite3_bind_text(stmt, 5, item.error.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, item.updated);
        sqlite3_bind_text(stmt, 7, item.alias.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(update) Failed to update record");
        }

        sqlite3_finalize(stmt);
    }

    void remove(const std::string& alias)
    {
        std::string sql = "DELETE FROM wingman WHERE alias = ?";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("(remove) Unable to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, alias.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("(remove) Failed to delete record");
        }

        sqlite3_finalize(stmt);
    }

    nlohmann::json toJson(const WingmanItem& item)
    {
        nlohmann::json j;
        j["alias"] = item.alias;
        j["status"] = wingmanItemStatusToString(item.status);
        j["modelRepo"] = item.modelRepo;
        j["filePath"] = item.filePath;
        j["force"] = item.force;
        j["error"] = item.error;
        j["created"] = item.created;
        j["updated"] = item.updated;

        return j;
    }

    WingmanItem fromJson(const nlohmann::json& j)
    {
        WingmanItem item;
        item.alias = j["alias"];
        item.status = stringToWingmanItemStatus(j["status"]);
        item.modelRepo = j["modelRepo"];
        item.filePath = j["filePath"];
        item.force = j["force"];
        item.error = j["error"];
        item.created = j["created"];
        item.updated = j["updated"];

        return item;
    }
};
