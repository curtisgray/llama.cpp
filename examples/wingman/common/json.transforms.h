#pragma once
#include "../json.hpp"
#include "../orm.hpp"
#include "download.server.types.h"

// Convert DownloadServerAppItem to JSON
nlohmann::json toJson(const DownloadServerAppItem& appItem);

// Convert JSON to DownloadServerAppItem
DownloadServerAppItem fromJsonToAppItem(const nlohmann::json& j);
