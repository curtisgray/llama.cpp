
#pragma once
#include <optional>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "util.hpp"

namespace wingman {

	// NOTE: all `created` and `updated` fields conform to the POSIX time standard, which
	//	is, according to <https://pubs.opengroup.org/onlinepubs/9699919799/functions/time.html>:
	//	"...seconds since the Epoch." The Epoc is 1970-01-01 00:00:00 +0000 (UTC).
	// TODO: move all times to milliseconds since the Epoch

	namespace fs = std::filesystem;

	template<typename T>
	std::optional<T> get_at_optional(const nlohmann::json &obj, const std::string &key) try {
		return obj.at(key).get<T>();
	} catch (...) {
		return std::nullopt;
	}

	struct AppItem {
		std::string isa = "AppItem";
		std::string name;
		std::string key;
		std::string value;
		int enabled;
		long long created;
		long long updated;

		AppItem() :
			key("default")
			, value("{}")
			, enabled(1)
			, created(util::now())
			, updated(util::now())
		{}

		static AppItem make(const std::string &name)
		{
			AppItem item;
			item.name = name;
			return item;
		}
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppItem, isa, name, key, value, enabled, created, updated);

	enum class DownloadItemStatus {
		idle,
		queued,
		downloading,
		complete,
		error,
		cancelled,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(DownloadItemStatus, {
		{DownloadItemStatus::unknown, "unknown"},
		{DownloadItemStatus::idle, "idle"},
		{DownloadItemStatus::queued, "queued"},
		{DownloadItemStatus::downloading, "downloading"},
		{DownloadItemStatus::complete, "complete"},
		{DownloadItemStatus::error, "error"},
		{DownloadItemStatus::cancelled, "cancelled"}
	})

	struct DownloadItemName {
		std::string isa = "DownloadItemName";
		std::string modelRepo;
		std::string filePath;
		std::string quantization;
		std::string quantizationName;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadItemName, isa, modelRepo, filePath, quantization, quantizationName);

	struct DownloadItem {
		std::string isa = "DownloadItem";
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
		long long totalBytes;
		long long downloadedBytes;
		std::string downloadSpeed;
		double progress;
		std::string error;
		long long created;
		long long updated;

		DownloadItem() :
			status(DownloadItemStatus::idle)
			, totalBytes(0)
			, downloadedBytes(0)
			, progress(0)
			, created(util::now())
			, updated(util::now())
		{}

		static DownloadItem make(const std::string &modelRepo, const std::string &filePath)
		{
			DownloadItem item;
			item.modelRepo = modelRepo;
			item.filePath = filePath;
			return item;
		}

		// write a function to return a string representation of the enum
		static std::string toString(DownloadItemStatus status)
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
				default:
					throw std::runtime_error("Unknown DownloadItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static DownloadItemStatus toStatus(const std::string &status)
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

		static DownloadItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadItem, isa, modelRepo, filePath, status, totalBytes, downloadedBytes, downloadSpeed, progress, error, created, updated);

	enum class WingmanItemStatus {
		idle,
		queued,
		preparing,
		inferring,
		complete,
		error,
		cancelling,
		cancelled,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(WingmanItemStatus, {
		{WingmanItemStatus::unknown, "unknown"},
		{WingmanItemStatus::idle, "idle"},
		{WingmanItemStatus::queued, "queued"},
		{WingmanItemStatus::preparing, "preparing"},
		{WingmanItemStatus::inferring, "inferring"},
		{WingmanItemStatus::complete, "complete"},
		{WingmanItemStatus::error, "error"},
		{WingmanItemStatus::cancelling, "cancelling"},
		{WingmanItemStatus::cancelled, "cancelled"}
	})

	struct WingmanItem {
		std::string isa = "WingmanItem";
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
		std::string address;
		int port;
		int contextSize;
		int gpuLayers;
		int force;
		std::string error;
		long long created;
		long long updated;

		WingmanItem() :
			status(WingmanItemStatus::idle)
		  , address("localhost")
		  , port(6567), contextSize(0), gpuLayers(-1), force(0)
		  , created(util::now())
		  , updated(util::now()) {}

		static WingmanItem make(const std::string &alias, const std::string &modelRepo, const std::string &filePath,
			const std::string &address, const int port, const int contextSize, const int gpuLayers, const int force)
		{
			WingmanItem item;
			item.alias = alias;
			item.status = WingmanItemStatus::idle;
			item.modelRepo = modelRepo;
			item.filePath = filePath;
			item.address = address;
			item.port = port;
			item.contextSize = contextSize;
			item.gpuLayers = gpuLayers;
			item.force = force;
			item.error = "";
			// set created and updated to the current time in unix milliseconds
			item.created = util::now();
			item.updated = util::now();
			return item;
		}

		// write a function to return a string representation of the enum
		static std::string toString(WingmanItemStatus status)
		{
			switch (status) {
				case WingmanItemStatus::idle:
					return "idle";
				case WingmanItemStatus::queued:
					return "queued";
				case WingmanItemStatus::preparing:
					return "preparing";
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
				default:
					throw std::runtime_error("Unknown DownloadItemStatus: " + std::to_string(static_cast<int>(status)));

			}
		}

		static WingmanItemStatus toStatus(const std::string &status)
		{
			if (status == "idle") {
				return WingmanItemStatus::idle;
			} else if (status == "queued") {
				return WingmanItemStatus::queued;
			} else if (status == "preparing") {
				return WingmanItemStatus::preparing;
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

		static WingmanItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WingmanItem, isa, alias, status, modelRepo, filePath, address, port, contextSize, gpuLayers, force, error, created, updated);

	struct DownloadedFileInfo {
		std::string modelRepo;
		std::string filePath;
		std::string status;
		long long totalBytes;
		long long downloadedBytes;
		std::string fileNameOnDisk;
		uintmax_t fileSizeOnDisk;
		std::string filePathOnDisk;
		long long created;
		long long updated;

		static DownloadedFileInfo make(const std::string &modelRepo, const std::string &filePath)
		{
			DownloadedFileInfo item;
			item.modelRepo = modelRepo;
			item.filePath = filePath;
			item.status = "unknown";
			item.totalBytes = 0;
			item.downloadedBytes = 0;
			item.fileNameOnDisk = "";
			item.fileSizeOnDisk = 0;
			item.filePathOnDisk = "";
			// set created and updated to the current time in unix milliseconds
			item.created = util::now();
			item.updated = util::now();
			return item;
		}
	};

	enum class DownloadServerAppItemStatus {
		ready,
		starting,
		preparing,
		downloading,
		stopping,
		stopped,
		error,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(DownloadServerAppItemStatus, {
		{DownloadServerAppItemStatus::unknown, "unknown"},
		{DownloadServerAppItemStatus::starting, "starting"},
		{DownloadServerAppItemStatus::preparing, "preparing"},
		{DownloadServerAppItemStatus::downloading, "downloading"},
		{DownloadServerAppItemStatus::stopping, "stopping"},
		{DownloadServerAppItemStatus::stopped, "stopped"},
		{DownloadServerAppItemStatus::error, "error"},
	})

	struct DownloadServerAppItem {
		std::string isa = "DownloadServerAppItem";
		DownloadServerAppItemStatus status;
		std::optional<DownloadItem> currentDownload;
		std::optional<std::string> error;
		long long created;
		long long updated;

		DownloadServerAppItem() :
			status(DownloadServerAppItemStatus::unknown)
			, created(util::now())
			, updated(util::now())
		{}

		static DownloadServerAppItem make()
		{
			DownloadServerAppItem item;
			return item;
		}

		static std::string toString(DownloadServerAppItemStatus status)
		{
			switch (status) {
				case DownloadServerAppItemStatus::ready:
					return "ready";
				case DownloadServerAppItemStatus::starting:
					return "starting";
				case DownloadServerAppItemStatus::preparing:
					return "preparing";
				case DownloadServerAppItemStatus::downloading:
					return "downloading";
				case DownloadServerAppItemStatus::stopping:
					return "stopping";
				case DownloadServerAppItemStatus::stopped:
					return "stopped";
				case DownloadServerAppItemStatus::error:
					return "error";
				case DownloadServerAppItemStatus::unknown:
					return "unknown";
				default:
					throw std::runtime_error("Unknown DownloadServerAppItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static DownloadServerAppItemStatus toStatus(const std::string &status)
		{
			if (status == "ready") {
				return DownloadServerAppItemStatus::ready;
			} else if (status == "starting") {
				return DownloadServerAppItemStatus::starting;
			} else if (status == "preparing") {
				return DownloadServerAppItemStatus::preparing;
			} else if (status == "downloading") {
				return DownloadServerAppItemStatus::downloading;
			} else if (status == "stopping") {
				return DownloadServerAppItemStatus::stopping;
			} else if (status == "stopped") {
				return DownloadServerAppItemStatus::stopped;
			} else if (status == "error") {
				return DownloadServerAppItemStatus::error;
			} else if (status == "unknown") {
				return DownloadServerAppItemStatus::unknown;
			} else {
				return DownloadServerAppItemStatus::unknown;
			}
		}

		static DownloadServerAppItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}

		// Convert DownloadServerAppItem to JSON
		static nlohmann::json toJson(const DownloadServerAppItem &downloadServerAppItem);

		// Convert JSON to DownloadServerAppItem
		static DownloadServerAppItem fromJson(const nlohmann::json &j);
	};

	// implement the nlohmann::json to_json and from_json functions manually to take the DownloadItem struct and DownloadItemStatus enum into account
	inline void to_json(nlohmann::json &j, const DownloadServerAppItem &downloadServerAppItem)
	{
		nlohmann::json currentDownload = nullptr;
		if (downloadServerAppItem.currentDownload) {
			to_json(currentDownload, downloadServerAppItem.currentDownload.value());
		}
		j = nlohmann::json{
			{"isa", downloadServerAppItem.isa},
			{"status", DownloadServerAppItem::toString(downloadServerAppItem.status)},
			{"currentDownload", currentDownload},
			{"error", downloadServerAppItem.error.value_or("")},
			{"created", downloadServerAppItem.created},
			{"updated", downloadServerAppItem.updated}
		};
	}

	inline void from_json(const nlohmann::json &j, DownloadServerAppItem &downloadServerAppItem)
	{
		// ensure currentDownload is not null
		if (j.contains("currentDownload") && !j.at("currentDownload").is_null()) {
			auto currentDownload = j.at("currentDownload").get<DownloadItem>();
			downloadServerAppItem.currentDownload.emplace(currentDownload);
		}
		if (j.contains("status") && !j.at("status").is_null()) {
			downloadServerAppItem.status = DownloadServerAppItem::toStatus(j.at("status").get<std::string>());
		}
		//downloadServerAppItem.status = DownloadServerAppItem::toStatus(j.at("status").get<std::string>());
		if (j.contains("error") && !j.at("error").is_null()) {
			downloadServerAppItem.error = j.at("error").get<std::string>();
		}
		if (j.contains("created") && !j.at("created").is_null()) {
			downloadServerAppItem.created = j.at("created").get<long long>();
		}
		if (j.contains("updated") && !j.at("updated").is_null()) {
			downloadServerAppItem.updated = j.at("updated").get<long long>();
		}
	}

	enum class WingmanServerAppItemStatus {
		ready,
		starting,
		preparing,
		inferring,
		stopping,
		stopped,
		error,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(WingmanServerAppItemStatus, {
		{WingmanServerAppItemStatus::unknown, "unknown"},
		{WingmanServerAppItemStatus::ready, "ready"},
		{WingmanServerAppItemStatus::starting, "starting"},
		{WingmanServerAppItemStatus::preparing, "preparing"},
		{WingmanServerAppItemStatus::inferring, "inferring"},
		{WingmanServerAppItemStatus::stopping, "stopping"},
		{WingmanServerAppItemStatus::stopped, "stopped"},
		{WingmanServerAppItemStatus::error, "error"},
	})

	struct WingmanServerAppItem {
		std::string isa = "WingmanServerAppItem";
		WingmanServerAppItemStatus status;
		std::string alias;
		std::string modelRepo;
		std::string filePath;
		bool force;
		std::optional<std::string> error;
		long long created;
		long long updated;

		WingmanServerAppItem() :
			status(WingmanServerAppItemStatus::unknown)
			, force(false)
			, created(util::now())
			, updated(util::now())
		{}

		static WingmanServerAppItem make()
		{
			WingmanServerAppItem item;
			return item;
		}

		static std::string toString(WingmanServerAppItemStatus status)
		{
			switch (status) {
				case WingmanServerAppItemStatus::ready:
					return "ready";
				case WingmanServerAppItemStatus::starting:
					return "starting";
				case WingmanServerAppItemStatus::preparing:
					return "preparing";
				case WingmanServerAppItemStatus::inferring:
					return "inferring";
				case WingmanServerAppItemStatus::stopping:
					return "stopping";
				case WingmanServerAppItemStatus::stopped:
					return "stopped";
				case WingmanServerAppItemStatus::error:
					return "error";
				case WingmanServerAppItemStatus::unknown:
					return "unknown";
				default:
					throw std::runtime_error("Unknown WingmanServerAppItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static WingmanServerAppItemStatus toStatus(const std::string &status)
		{
			if (status == "ready") {
				return WingmanServerAppItemStatus::ready;
			} else if (status == "starting") {
				return WingmanServerAppItemStatus::starting;
			} else if (status == "preparing") {
				return WingmanServerAppItemStatus::preparing;
			} else if (status == "inferring") {
				return WingmanServerAppItemStatus::inferring;
			} else if (status == "stopping") {
				return WingmanServerAppItemStatus::stopping;
			} else if (status == "stopped") {
				return WingmanServerAppItemStatus::stopped;
			} else if (status == "error") {
				return WingmanServerAppItemStatus::error;
			} else if (status == "unknown") {
				return WingmanServerAppItemStatus::unknown;
			} else {
				return WingmanServerAppItemStatus::unknown;
			}
		}

		static WingmanServerAppItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}

		// Convert WingmanServerAppItem to JSON
		static nlohmann::json toJson(const WingmanServerAppItem &wingmanServerAppItem);

		// Convert JSON to WingmanServerAppItem
		static WingmanServerAppItem fromJson(const nlohmann::json &j);
	};

	// implement the nlohmann::json to_json and from_json functions manually to take the WingmanItem struct and WingmanItemStatus enum into account
	// ReSharper disable once CppInconsistentNaming
	inline void to_json(nlohmann::json &j, const WingmanServerAppItem &wingmanServerAppItem)
	{
		j = nlohmann::json{
			{"isa", wingmanServerAppItem.isa},
			{"alias", wingmanServerAppItem.alias},
			{"modelRepo", wingmanServerAppItem.modelRepo},
			{"filePath", wingmanServerAppItem.filePath},
			{"force", wingmanServerAppItem.force},
			{"status", WingmanServerAppItem::toString(wingmanServerAppItem.status)},
			{"error", wingmanServerAppItem.error.value_or("")},
			{"created", wingmanServerAppItem.created},
			{"updated", wingmanServerAppItem.updated}
		};
	}

	// ReSharper disable once CppInconsistentNaming
	inline void from_json(const nlohmann::json &j, WingmanServerAppItem &wingmanServerAppItem)
	{
		if (j.contains("alias")) {
			wingmanServerAppItem.alias = j.at("alias").get<std::string>();
		}
		if (j.contains("modelRepo")) {
			wingmanServerAppItem.modelRepo = j.at("modelRepo").get<std::string>();
		}
		if (j.contains("filePath")) {
			wingmanServerAppItem.filePath = j.at("filePath").get<std::string>();
		}
		if (j.contains("force")) {
			wingmanServerAppItem.force = j.at("force").get<bool>();
		}
		if (j.contains("status")) {
			wingmanServerAppItem.status = WingmanServerAppItem::toStatus(j.at("status").get<std::string>());
		}
		if (j.contains("error")) {
			wingmanServerAppItem.error = j.at("error").get<std::string>();
		}
		if (j.contains("created")) {
			wingmanServerAppItem.created = j.at("created").get<long long>();
		}
		if (j.contains("updated")) {
			wingmanServerAppItem.updated = j.at("updated").get<long long>();
		}
	}

	struct DownloadableItem {
		std::string isa = "DownloadableItem";
		std::string modelRepo;
		std::string modelRepoName;
		std::string filePath;
		std::string quantization;
		std::string quantizationName;
		bool isDownloaded = false;
		bool available = false;
		std::string location;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadableItem, isa, modelRepo, modelRepoName, filePath, quantization, quantizationName, isDownloaded, location);

	struct AIModel {
		std::string isa = "AIModel";
		std::string id;
		std::string name;
		int maxLength;
		int tokenLimit;
		std::string vendor;
		std::string location;
		std::string apiKey; // this is the api key for commercial models. [NOTE: it is removed from the json when serialized]
		std::vector<DownloadableItem> items;
		DownloadableItem item; // this is the item that is currently selected. [NOTE: it is removed from the json when serialized]
	};
	//NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AIModel, id, name, maxLength, tokenLimit, vendor, location, apiKey, item, items);
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AIModel, isa, id, name, maxLength, tokenLimit, vendor, location, items);

	inline std::string GetHomeEnvVar()
	{
		std::string key;
#ifdef _WIN32
		key = "USERPROFILE";
#else
		key = "HOME";
#endif

		return std::string(::getenv(key.c_str()));
	}

	inline fs::path GetWingmanHome()
	{
		const auto home = fs::path(GetHomeEnvVar());
		return home / ".wingman";
	}
} // namespace wingman
