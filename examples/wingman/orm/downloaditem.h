#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace fs = std::filesystem;

class DownloadItemOrm {
private:
    sqlite3* db;
    DatabaseActions dbActions;
    static fs::path downloadsDirectory;

    std::vector<DownloadItemStatus> activeDownloadStatuses = { DownloadItemStatus::queued, DownloadItemStatus::downloading };

public:
    DownloadItemOrm(sqlite3* dbInstance, std::string& downloadsDirectory);

    void upsert(const DownloadItem& item);
    void remove(const std::string& modelRepo, const std::string& filePath);

    std::optional<DownloadItem> get(const std::string& modelRepo, const std::string& filePath);

    std::vector<DownloadItem> getAll();

    void reset();

    static std::string getDownloadItemFileName(const std::string& modelRepo, const std::string& filePath);

    static bool isDownloaded(const std::string& modelRepo, const std::string& filePath);

    static DownloadedFileInfo getDownloadedFileInfo(const std::string& modelRepo, const std::string& filePath);

    static std::vector<std::string> getModelFiles();

    static std::vector<DownloadedFileInfo> getDownloadedFileInfos();

    static std::string safeDownloadItemName(const std::string& modelRepo, const std::string& filePath);

    static std::optional<DownloadItemName> safeDownloadItemNameToModelRepo(const std::string& name);

    static std::string getDownloadItemFilePath(const std::string& modelRepo, const std::string& filePath);
};
