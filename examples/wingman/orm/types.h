
#pragma once
#include <map>
#include <optional>
#include <string>

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

struct DownloadedFileInfo {
    std::string modelRepo;
    std::string filePath;
    std::string status;
    int totalBytes;
    int downloadedBytes;
    std::string fileNameOnDisk;
    int fileSizeOnDisk;
    std::string filePathOnDisk;
    long long created;
    long long updated;
};

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

std::string get_home_env_var()
{
    std::string key;
#ifdef _WIN32
    key = "USERPROFILE";
#else
    key = "HOME";
#endif

    return std::string(getenv(key.c_str()));
}

std::string get_wingman_home()
{
    std::string home = get_home_env_var();
    return home + "/.wingman";
}
