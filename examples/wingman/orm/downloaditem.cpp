#include "common.h"
#include <regex>

DownloadItemOrm::DownloadItemOrm(sqlite3* dbInstance, std::string& downloadsDirectory)
    : db(dbInstance)
    , dbActions(dbInstance)
{
    DownloadItemOrm::downloadsDirectory = downloadsDirectory;
    fs::create_directories(downloadsDirectory);
}

void DownloadItemOrm::upsert(const DownloadItem& item)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(insert) Inserting download for modelRepo: " + item.modelRepo + " and filePath: " + item.filePath);

    std::string sql = "INSERT OR REPLACE INTO downloads (modelRepo, filePath, status, totalBytes, downloadedBytes, "
                      "downloadSpeed, progress, error, created, updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, item.modelRepo.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, item.filePath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, downloadItemStatusToString(item.status).c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, item.totalBytes);
    sqlite3_bind_int(stmt, 5, item.downloadedBytes);
    sqlite3_bind_text(stmt, 6, item.downloadSpeed.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 7, item.progress);
    sqlite3_bind_text(stmt, 8, item.error.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 9, item.created);
    sqlite3_bind_int64(stmt, 10, item.updated);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(insert) Download item inserted.");
}

void DownloadItemOrm::remove(const std::string& modelRepo, const std::string& filePath)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(remove) Removing download for modelRepo: " + modelRepo + " and filePath: " + filePath);

    std::string sql = "DELETE FROM downloads WHERE modelRepo = ? AND filePath = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, modelRepo.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, filePath.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to delete record.");
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(remove) Download item removed.");
}

std::optional<DownloadItem> DownloadItemOrm::get(const std::string& modelRepo, const std::string& filePath)
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(get) Retrieving download for modelRepo: " + modelRepo + " and filePath: " + filePath);

    std::string sql = "SELECT * FROM downloads WHERE modelRepo = ? AND filePath = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    sqlite3_bind_text(stmt, 1, modelRepo.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, filePath.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DownloadItem item;
        // Populate the DownloadItem struct with data from the database
        item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.status = stringToDownloadItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        item.totalBytes = sqlite3_column_int(stmt, 3);
        item.downloadedBytes = sqlite3_column_int(stmt, 4);
        item.downloadSpeed = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        item.progress = sqlite3_column_double(stmt, 6);
        item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        item.created = sqlite3_column_int64(stmt, 8);
        item.updated = sqlite3_column_int64(stmt, 9);

        sqlite3_finalize(stmt);
        spdlog::debug("(get) Download item retrieved.");
        return item; // Returns the populated item inside an optional.
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(get) Download item not found.");
    return {}; // Returns an empty optional.
}

std::vector<DownloadItem> DownloadItemOrm::getAll()
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(getAll) Retrieving all downloads");

    std::string sql = "SELECT * FROM downloads";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement.");
    }

    std::vector<DownloadItem> items;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DownloadItem item;
        item.modelRepo = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.status = stringToDownloadItemStatus(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        item.totalBytes = sqlite3_column_int(stmt, 3);
        item.downloadedBytes = sqlite3_column_int(stmt, 4);
        item.downloadSpeed = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        item.progress = sqlite3_column_double(stmt, 6);
        item.error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        item.created = sqlite3_column_int64(stmt, 8);
        item.updated = sqlite3_column_int64(stmt, 9);
        items.push_back(item);
    }

    sqlite3_finalize(stmt);
    spdlog::debug("(getAll) All download items retrieved.");

    return items;
}

void DownloadItemOrm::reset()
{
    if (db == nullptr) {
        throw std::runtime_error("ORM not initialized");
    }

    spdlog::debug("(reset) Resetting downloads table");

    // mark all idle,downloading and error items as queued
    // delete other items

    std::string sql = "UPDATE downloads SET status = 'queued', progress = 0, downloadedBytes = 0, totalBytes = 0, downloadSpeed = '' WHERE status = 'downloading' OR status = 'error' or status = 'idle'";

    char* errorMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(reset) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }

    sql = "DELETE FROM downloads WHERE status = 'complete' OR status = 'cancelled' OR status = 'unknown'";
    errorMsg = nullptr;
    rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg);
    if (rc != SQLITE_OK) {
        std::string errorString = "(reset) Exception: " + std::string(errorMsg);
        sqlite3_free(errorMsg);
        throw std::runtime_error(errorString);
    }
    spdlog::debug("(reset) Downloads table reset.");
}

std::string DownloadItemOrm::getDownloadItemFileName(const std::string& modelRepo, const std::string& filePath)
{
    return safeDownloadItemName(modelRepo, filePath);
}

bool DownloadItemOrm::isDownloaded(const std::string& modelRepo, const std::string& filePath)
{
    // auto item = get(modelRepo, filePath);
    OrmFactory ormFactory;
    auto item = ormFactory.downloadOrm()->get(modelRepo, filePath);

    // If it's in the database and marked as complete, then it's downloaded.
    if (item.has_value() && item->status == DownloadItemStatus::complete) {
        return true;
    }

    // If it's not in the database, we check the file system.
    return std::filesystem::exists(downloadsDirectory / filePath);
}

DownloadedFileInfo DownloadItemOrm::getDownloadedFileInfo(const std::string& modelRepo, const std::string& filePath)
{
    // auto item = get(modelRepo, filePath);
    OrmFactory ormFactory;
    auto item = ormFactory.downloadOrm()->get(modelRepo, filePath);

    DownloadedFileInfo fileInfo;
    fileInfo.filePath = filePath;
    fileInfo.modelRepo = modelRepo;

    // If the item is in the database, fetch its information.
    if (item.has_value()) {
        fileInfo.totalBytes = item->totalBytes;
        fileInfo.downloadedBytes = item->downloadedBytes;
    }

    auto safeFileName = DownloadItemOrm::safeDownloadItemName(modelRepo, filePath);
    auto path = downloadsDirectory / filePath;
    // Getting the size directly from the file system.
    fileInfo.fileSizeOnDisk = std::filesystem::file_size(downloadsDirectory / fs::path(filePath));

    return fileInfo;
}

std::vector<std::string> DownloadItemOrm::getModelFiles()
{
    std::vector<std::string> files;

    for (const auto& entry : std::filesystem::directory_iterator(downloadsDirectory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().filename().string());
        }
    }

    return files;
}

std::vector<DownloadedFileInfo> DownloadItemOrm::getDownloadedFileInfos()
{
    std::vector<DownloadedFileInfo> fileInfos;
    auto modelFiles = getModelFiles();

    for (const auto& file : modelFiles) {
        auto name = safeDownloadItemNameToModelRepo(file);
        if (!name) {
            spdlog::debug("Skipping file: " + file + " because it's not a downloaded model file.");
            continue;
        }
        fileInfos.push_back(getDownloadedFileInfo(name->modelRepo, name->filePath));
    }

    return fileInfos;
}

std::string DownloadItemOrm::safeDownloadItemName(const std::string& modelRepo, const std::string& filePath)
{
    std::regex slashRegex("\\/");
    std::string result = std::regex_replace(modelRepo, slashRegex, "[-]");
    return result + "[=]" + filePath;
}

std::optional<DownloadItemName> DownloadItemOrm::safeDownloadItemNameToModelRepo(const std::string& name)
{
    if (name.find("[-]") == std::string::npos || name.find("[=]") == std::string::npos) {
        return {};
    }

    size_t pos = name.find("[=]");
    std::string modelRepoPart = name.substr(0, pos);
    std::string filePathPart = name.substr(pos + 3);

    std::regex dashRegex("\\[-\\]");
    modelRepoPart = std::regex_replace(modelRepoPart, dashRegex, "/");

    return { DownloadItemName { modelRepoPart, filePathPart } }; // Return the struct and true flag indicating success.
}

std::string DownloadItemOrm::getDownloadItemFilePath(const std::string& modelRepo, const std::string& filePath)
{
    std::filesystem::path path = downloadsDirectory / safeDownloadItemName(modelRepo, filePath);
    return path.string();
}
