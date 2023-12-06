#include "types.h"

#include "orm.h"

namespace wingman {
	nlohmann::json DownloadServiceAppItem::toJson(const DownloadServiceAppItem &downloadServerAppItem)
	{
		nlohmann::json j;
		j["isa"] = downloadServerAppItem.isa;
		j["status"] = DownloadServiceAppItem::toString(downloadServerAppItem.status);
		if (downloadServerAppItem.currentDownload) {
			j["currentDownload"] = orm::DownloadItemActions::toJson(downloadServerAppItem.currentDownload.value());
		}
		if (downloadServerAppItem.error) {
			j["error"] = downloadServerAppItem.error.value();
		}
		j["created"] = downloadServerAppItem.created;
		j["updated"] = downloadServerAppItem.updated;
		return j;
	}

	DownloadServiceAppItem DownloadServiceAppItem::fromJson(const nlohmann::json &j)
	{
		DownloadServiceAppItem downloadServerAppItem;
		downloadServerAppItem.status = DownloadServiceAppItem::toStatus(j["status"].get<std::string>());
		if (j.contains("currentDownload")) {
			auto currentDownload = orm::DownloadItemActions::fromJson(j["currentDownload"]);
			downloadServerAppItem.currentDownload.emplace(currentDownload);
		}
		if (j.contains("error")) {
			downloadServerAppItem.error = j["error"].get<std::string>();
		}
		downloadServerAppItem.created = j["created"].get<long long>();
		downloadServerAppItem.updated = j["updated"].get<long long>();
		return downloadServerAppItem;
	}
}
