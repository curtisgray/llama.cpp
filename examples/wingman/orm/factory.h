#pragma once
#include "app.h"
#include "downloaditem.h"
#include "wingmanitem.h"
#include <memory>
#include <optional>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

class OrmFactory {
    sqlite3* db;
    const std::string ORM_NAME = "orm.Sqlite";
    bool initialized;

public:
    std::shared_ptr<AppOrm> pAppOrm;
    std::shared_ptr<DownloadItemOrm> pDownloadItemOrm;
    std::shared_ptr<WingmanItemOrm> pWingmanItemOrm;

    void openDatabase(const std::string& dbPath);

    void initializeDatabase(const std::string& dbPath);
public:
    OrmFactory(std::optional<const std::string> baseDirectory = std::nullopt);

    ~OrmFactory();

    const std::shared_ptr<AppOrm> appOrm();

    const std::shared_ptr<DownloadItemOrm> downloadOrm();

    const std::shared_ptr<WingmanItemOrm> wingmanOrm();
};
