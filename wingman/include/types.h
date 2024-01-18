
// ReSharper disable CppClangTidyClangDiagnosticSwitchEnum
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

	class CudaOutOfMemory: public std::runtime_error {
	public:
		explicit CudaOutOfMemory(const std::string &message) : std::runtime_error(message) {}
	};

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
		queued,
		preparing,
		inferring,
		complete,
		error,
		cancelling,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(WingmanItemStatus, {
		{WingmanItemStatus::unknown, "unknown"},
		{WingmanItemStatus::queued, "queued"},
		{WingmanItemStatus::preparing, "preparing"},
		{WingmanItemStatus::inferring, "inferring"},
		{WingmanItemStatus::complete, "complete"},
		{WingmanItemStatus::error, "error"},
		{WingmanItemStatus::cancelling, "cancelling"},
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
			status(WingmanItemStatus::unknown)
		  , address("localhost")
		  , port(6567), contextSize(0), gpuLayers(-1), force(0)
		  , created(util::now())
		  , updated(util::now()) {}

		static WingmanItem make(const std::string &alias, const std::string &modelRepo, const std::string &filePath,
			const std::string &address, const int port, const int contextSize, const int gpuLayers, const int force)
		{
			WingmanItem item;
			item.alias = alias;
			item.status = WingmanItemStatus::unknown;
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
				case WingmanItemStatus::unknown:
					return "unknown";
				default:
					throw std::runtime_error("Unknown DownloadItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static WingmanItemStatus toStatus(const std::string &status)
		{
			if (status == "queued") {
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
			} else {
				return WingmanItemStatus::unknown;
			}
		}

		static WingmanItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}

		static bool isCancelling(const WingmanItem& item)
		{
			return item.status == WingmanItemStatus::cancelling;
		}

		static bool hasActiveStatus(const WingmanItem& item)
		{
			switch (item.status) {
				case WingmanItemStatus::queued:
				case WingmanItemStatus::preparing:
				case WingmanItemStatus::inferring:
					return true;
				default:
					return false;
			}
		}

		// check a list of Wingman items to see if any of them have an active status
		static bool hasActiveStatus(const std::vector<WingmanItem>& items)
		{
			for (const auto& item : items) {
				if (!hasActiveStatus(item)) {
					return false;
				}
			}
			return true;
		}

		// completed status
		static bool hasCompletedStatus(const WingmanItem& item)
		{
			switch (item.status) {
				case WingmanItemStatus::complete:
				case WingmanItemStatus::error:
					return true;
				default:
					return false;
			}
		}

		static bool hasCompletedStatus(const std::vector<WingmanItem>& items)
		{
			for (const auto& item : items) {
				if (!hasCompletedStatus(item)) {
					return false;
				}
			}
			return true;
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

	enum class DownloadServiceAppItemStatus {
		ready,
		starting,
		preparing,
		downloading,
		stopping,
		stopped,
		error,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(DownloadServiceAppItemStatus, {
		{DownloadServiceAppItemStatus::unknown, "unknown"},
		{DownloadServiceAppItemStatus::starting, "starting"},
		{DownloadServiceAppItemStatus::preparing, "preparing"},
		{DownloadServiceAppItemStatus::downloading, "downloading"},
		{DownloadServiceAppItemStatus::stopping, "stopping"},
		{DownloadServiceAppItemStatus::stopped, "stopped"},
		{DownloadServiceAppItemStatus::error, "error"},
	})

	struct DownloadServiceAppItem {
		std::string isa = "DownloadServiceAppItem";
		DownloadServiceAppItemStatus status;
		std::optional<DownloadItem> currentDownload;
		std::optional<std::string> error;
		long long created;
		long long updated;

		DownloadServiceAppItem() :
			status(DownloadServiceAppItemStatus::unknown)
			, created(util::now())
			, updated(util::now())
		{}

		static DownloadServiceAppItem make()
		{
			DownloadServiceAppItem item;
			return item;
		}

		static std::string toString(DownloadServiceAppItemStatus status)
		{
			switch (status) {
				case DownloadServiceAppItemStatus::ready:
					return "ready";
				case DownloadServiceAppItemStatus::starting:
					return "starting";
				case DownloadServiceAppItemStatus::preparing:
					return "preparing";
				case DownloadServiceAppItemStatus::downloading:
					return "downloading";
				case DownloadServiceAppItemStatus::stopping:
					return "stopping";
				case DownloadServiceAppItemStatus::stopped:
					return "stopped";
				case DownloadServiceAppItemStatus::error:
					return "error";
				case DownloadServiceAppItemStatus::unknown:
					return "unknown";
				default:
					throw std::runtime_error("Unknown DownloadServiceAppItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static DownloadServiceAppItemStatus toStatus(const std::string &status)
		{
			if (status == "ready") {
				return DownloadServiceAppItemStatus::ready;
			} else if (status == "starting") {
				return DownloadServiceAppItemStatus::starting;
			} else if (status == "preparing") {
				return DownloadServiceAppItemStatus::preparing;
			} else if (status == "downloading") {
				return DownloadServiceAppItemStatus::downloading;
			} else if (status == "stopping") {
				return DownloadServiceAppItemStatus::stopping;
			} else if (status == "stopped") {
				return DownloadServiceAppItemStatus::stopped;
			} else if (status == "error") {
				return DownloadServiceAppItemStatus::error;
			} else if (status == "unknown") {
				return DownloadServiceAppItemStatus::unknown;
			} else {
				return DownloadServiceAppItemStatus::unknown;
			}
		}

		static DownloadServiceAppItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}

		// Convert DownloadServiceAppItem to JSON
		static nlohmann::json toJson(const DownloadServiceAppItem &downloadServerAppItem);

		// Convert JSON to DownloadServiceAppItem
		static DownloadServiceAppItem fromJson(const nlohmann::json &j);
	};

	// implement the nlohmann::json to_json and from_json functions manually to take the DownloadItem struct and DownloadItemStatus enum into account
	inline void to_json(nlohmann::json &j, const DownloadServiceAppItem &downloadServerAppItem)
	{
		nlohmann::json currentDownload = nullptr;
		if (downloadServerAppItem.currentDownload) {
			to_json(currentDownload, downloadServerAppItem.currentDownload.value());
		}
		j = nlohmann::json{
			{"isa", downloadServerAppItem.isa},
			{"status", DownloadServiceAppItem::toString(downloadServerAppItem.status)},
			{"currentDownload", currentDownload},
			{"error", downloadServerAppItem.error.value_or("")},
			{"created", downloadServerAppItem.created},
			{"updated", downloadServerAppItem.updated}
		};
	}

	inline void from_json(const nlohmann::json &j, DownloadServiceAppItem &downloadServerAppItem)
	{
		// ensure currentDownload is not null
		if (j.contains("currentDownload") && !j.at("currentDownload").is_null()) {
			auto currentDownload = j.at("currentDownload").get<DownloadItem>();
			downloadServerAppItem.currentDownload.emplace(currentDownload);
		}
		if (j.contains("status") && !j.at("status").is_null()) {
			downloadServerAppItem.status = DownloadServiceAppItem::toStatus(j.at("status").get<std::string>());
		}
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

	enum class WingmanServiceAppItemStatus {
		ready,
		starting,
		preparing,
		inferring,
		stopping,
		stopped,
		error,
		unknown
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(WingmanServiceAppItemStatus, {
		{WingmanServiceAppItemStatus::unknown, "unknown"},
		{WingmanServiceAppItemStatus::ready, "ready"},
		{WingmanServiceAppItemStatus::starting, "starting"},
		{WingmanServiceAppItemStatus::preparing, "preparing"},
		{WingmanServiceAppItemStatus::inferring, "inferring"},
		{WingmanServiceAppItemStatus::stopping, "stopping"},
		{WingmanServiceAppItemStatus::stopped, "stopped"},
		{WingmanServiceAppItemStatus::error, "error"},
	})

	struct WingmanServiceAppItem {
		std::string isa = "WingmanServiceAppItem";
		WingmanServiceAppItemStatus status;
		std::string alias;
		std::string modelRepo;
		std::string filePath;
		bool force;
		std::optional<std::string> error;
		long long created;
		long long updated;

		WingmanServiceAppItem() :
			status(WingmanServiceAppItemStatus::unknown)
			, force(false)
			, created(util::now())
			, updated(util::now())
		{}

		static WingmanServiceAppItem make()
		{
			WingmanServiceAppItem item;
			return item;
		}

		static std::string toString(WingmanServiceAppItemStatus status)
		{
			switch (status) {
				case WingmanServiceAppItemStatus::ready:
					return "ready";
				case WingmanServiceAppItemStatus::starting:
					return "starting";
				case WingmanServiceAppItemStatus::preparing:
					return "preparing";
				case WingmanServiceAppItemStatus::inferring:
					return "inferring";
				case WingmanServiceAppItemStatus::stopping:
					return "stopping";
				case WingmanServiceAppItemStatus::stopped:
					return "stopped";
				case WingmanServiceAppItemStatus::error:
					return "error";
				case WingmanServiceAppItemStatus::unknown:
					return "unknown";
				default:
					throw std::runtime_error("Unknown WingmanServiceAppItemStatus: " + std::to_string(static_cast<int>(status)));
			}
		}

		static WingmanServiceAppItemStatus toStatus(const std::string &status)
		{
			if (status == "ready") {
				return WingmanServiceAppItemStatus::ready;
			} else if (status == "starting") {
				return WingmanServiceAppItemStatus::starting;
			} else if (status == "preparing") {
				return WingmanServiceAppItemStatus::preparing;
			} else if (status == "inferring") {
				return WingmanServiceAppItemStatus::inferring;
			} else if (status == "stopping") {
				return WingmanServiceAppItemStatus::stopping;
			} else if (status == "stopped") {
				return WingmanServiceAppItemStatus::stopped;
			} else if (status == "error") {
				return WingmanServiceAppItemStatus::error;
			} else if (status == "unknown") {
				return WingmanServiceAppItemStatus::unknown;
			} else {
				return WingmanServiceAppItemStatus::unknown;
			}
		}

		static WingmanServiceAppItemStatus toStatus(const unsigned char *input)
		{
			const std::string status(reinterpret_cast<const char *>(input));
			return toStatus(status);
		}

		// Convert WingmanServiceAppItem to JSON
		static nlohmann::json toJson(const WingmanServiceAppItem &wingmanServerAppItem);

		// Convert JSON to WingmanServiceAppItem
		static WingmanServiceAppItem fromJson(const nlohmann::json &j);
	};

	// implement the nlohmann::json to_json and from_json functions manually to take the WingmanItem struct and WingmanItemStatus enum into account
	// ReSharper disable once CppInconsistentNaming
	inline void to_json(nlohmann::json &j, const WingmanServiceAppItem &wingmanServerAppItem)
	{
		j = nlohmann::json{
			{"isa", wingmanServerAppItem.isa},
			{"alias", wingmanServerAppItem.alias},
			{"modelRepo", wingmanServerAppItem.modelRepo},
			{"filePath", wingmanServerAppItem.filePath},
			{"force", wingmanServerAppItem.force},
			{"status", WingmanServiceAppItem::toString(wingmanServerAppItem.status)},
			{"error", wingmanServerAppItem.error.value_or("")},
			{"created", wingmanServerAppItem.created},
			{"updated", wingmanServerAppItem.updated}
		};
	}

	// ReSharper disable once CppInconsistentNaming
	inline void from_json(const nlohmann::json &j, WingmanServiceAppItem &wingmanServerAppItem)
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
			wingmanServerAppItem.status = WingmanServiceAppItem::toStatus(j.at("status").get<std::string>());
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
		bool hasError = false;
		std::string location;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadableItem, isa, modelRepo, modelRepoName, filePath, quantization, quantizationName, isDownloaded, available, hasError, location);

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

	constexpr auto DEFAULT_CONTEXT_LENGTH = 4096;

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
