#include "factory.h"

void OrmFactory::openDatabase(const std::string& dbPath)
{
    spdlog::debug("(openDatabase) Opening database " + dbPath + "...");
    if (db != nullptr) {
        throw std::runtime_error("(openDatabase) Database is already opened.");
    }
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("(openDatabase) Failed to open database: " + std::string(sqlite3_errmsg(db)));
    }
    spdlog::debug("(openDatabase) Database opened.");
}

void OrmFactory::initializeDatabase(const std::string& dbPath)
{
    spdlog::debug("(initializeDatabase) Initializing database...");

    if (initialized) {
        throw std::runtime_error("(initializeDatabase) ORM already initialized");
    }

    std::string dataDir = dbPath;
    spdlog::debug("(initializeDatabase) DATA_DIR: " + dataDir);

    // Ensure the directory exists
    spdlog::debug("(initializeDatabase) Ensuring DATA_DIR exists...");
    fs::create_directories(dataDir);
    spdlog::trace("(initializeDatabase) DATA_DIR exists...");

    openDatabase(dbPath);

    initialized = true;
}

OrmFactory::OrmFactory(std::optional<const std::string> baseDirectory)
    : initialized(false)
{
    std::string baseDir = baseDirectory.value_or(get_wingman_home());
    std::string dataDir = baseDir + "/data";
    std::string modelsDir = baseDir + "/models";
    // spdlog levels:
    // trace = SPDLOG_LEVEL_TRACE,
    // debug = SPDLOG_LEVEL_DEBUG,
    // info = SPDLOG_LEVEL_INFO,
    // warn = SPDLOG_LEVEL_WARN,
    // err = SPDLOG_LEVEL_ERROR,
    // critical = SPDLOG_LEVEL_CRITICAL,
    // off = SPDLOG_LEVEL_OFF,
    // For convenience, spdlog creates a default global logger (to stdout, colored and multithreaded).
    //  It can be used easily by calling spdlog::info(..), spdlog::debug(..), etc directly.
    //  It's instance can be replaced to any other logger (shared_ptr):
    //   spdlog::set_default_logger(some_other_logger);
    spdlog::info("Starting ORM...");
    initializeDatabase(dataDir + "/wingman.db");

    auto dbActions = DatabaseActions(db);

    dbActions.createDownloadsTable();
    dbActions.createWingmanTable();
    dbActions.createAppTable();

    pAppOrm = std::make_shared<AppOrm>(db, *this);
    pDownloadItemOrm = std::make_shared<DownloadItemOrm>(db, *this, modelsDir);
    pWingmanItemOrm = std::make_shared<WingmanItemOrm>(db, *this, modelsDir);
}

OrmFactory::~OrmFactory()
{
    if (db != nullptr) {
        sqlite3_close(db);
    }
}

const std::shared_ptr < AppOrm> OrmFactory::appOrm()
{
    return pAppOrm;
}

const std::shared_ptr < DownloadItemOrm> OrmFactory::downloadOrm()
{
    return pDownloadItemOrm;
}

const std::shared_ptr < WingmanItemOrm> OrmFactory::wingmanOrm()
{
    return pWingmanItemOrm;
}
