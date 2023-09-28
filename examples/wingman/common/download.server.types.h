#pragma once
#include <optional>
#include <string>
#include "../orm.h"

struct DownloadServerAppItem {
    std::string isa = "DownloadServer";
    std::string status;
    std::optional<DownloadItem> currentDownload;
    std::optional<std::string> error;
    long long created;
    long long updated;
};
