#include "json.transforms.h"

// Convert DownloadServerAppItem to JSON
nlohmann::json toJson(const DownloadServerAppItem& appItem)
{
    nlohmann::json j;
    j["isa"] = appItem.isa;
    j["status"] = appItem.status;
    if (appItem.currentDownload.has_value()) {
        j["currentDownload"] = toJson(appItem.currentDownload.value());
    }
    if (appItem.error.has_value()) {
        j["error"] = appItem.error.value();
    }
    j["created"] = appItem.created;
    j["updated"] = appItem.updated;
    return j;
}

// Convert JSON to DownloadServerAppItem
DownloadServerAppItem fromJsonToAppItem(const nlohmann::json& j)
{
    DownloadServerAppItem appItem;
    appItem.isa = j["isa"].get<std::string>();
    appItem.status = j["status"].get<std::string>();
    if (j.contains("currentDownload")) {
        appItem.currentDownload = fromJsonToDownloadItem(j["currentDownload"]);
    }
    if (j.contains("error")) {
        appItem.error = j["error"].get<std::string>();
    }
    appItem.created = j["created"].get<long long>();
    appItem.updated = j["updated"].get<long long>();
    return appItem;
}
