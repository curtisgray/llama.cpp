#include <chrono>
#include <thread>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "download.service.h"

namespace wingman::services {
	DownloadService::DownloadService(orm::ItemActionsFactory &actionsFactory
			, const std::function<bool(curl::Response *)> &onDownloadProgress
			, const std::function<bool(DownloadServerAppItem *)> &onServiceStatus)
		: actions(actionsFactory)
		, onDownloadProgress(onDownloadProgress)
		, onServiceStatus(onServiceStatus)
	{}

	void DownloadService::startDownload(const DownloadItem &downloadItem, bool overwrite)
	{
		const auto url = orm::DownloadItemActions::urlForModel(downloadItem);
		const auto item = std::make_shared<DownloadItem>(DownloadItem{ downloadItem });
		auto request = curl::Request{ url };
		request.file.item = item;
		request.file.actions = actions.download();
		request.file.onProgress = [this](curl::Response *response) {
			if (keepDownloading) {
				if (onDownloadProgress) {
					return onDownloadProgress(response);
				}
				return true;
			}
			return false;
		};
		request.file.overwrite = overwrite;

		keepDownloading = true;
		const auto response = Fetch(request);
	}

	void DownloadService::stopDownload(const DownloadItem &downloadItem)
	{
		keepDownloading = false;
	}

	void DownloadService::updateServerStatus(const DownloadServerAppItemStatus &status, std::optional<DownloadItem> downloadItem, std::optional<std::string> error)
	{
		auto appItem = actions.app()->get(SERVER_NAME).value_or(AppItem::make(SERVER_NAME));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto downloadServerItem = j.get<DownloadServerAppItem>();
		downloadServerItem.status = status;
		if (error) {
			downloadServerItem.error = error;
		}
		if (downloadItem) {
			downloadServerItem.currentDownload.emplace(downloadItem.value());
		}
		if (onServiceStatus) {
			if (!onServiceStatus(&downloadServerItem)) {
				spdlog::debug(SERVER_NAME + ": (updateServerStatus) onServiceStatus returned false, stopping server.");
				stop();
			}
		}
		nlohmann::json j2 = downloadServerItem;
		appItem.value = j2.dump();
		actions.app()->set(appItem);
	}

	void DownloadService::runOrphanedDownloadCleanup() const
	{
		// Check for orphaned downloads and clean up
		for (const auto downloads = actions.download()->getAll(); const auto & download : downloads) {
			if (download.status == DownloadItemStatus::complete) {
				// Check if the download file exists in the file system
				if (!actions.download()->fileExists(download)) {
					actions.download()->remove(download.modelRepo, download.filePath);
				}
			}
		}
		// Check for orphaned downloaded model files on disk and clean up
		for (const auto files = orm::DownloadItemActions::getModelFiles(); const auto & file : files) {
			// get file names from disk and check if they are in the database
			if (const auto din = orm::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(file)) {
				const auto downloadItem = actions.download()->get(din.value().modelRepo, din.value().filePath);
				if (!downloadItem) {
					// get full path to file and remove it
					const auto fullPath = orm::DownloadItemActions::getDownloadItemOutputPath(din.value().modelRepo, din.value().filePath);
					spdlog::info(SERVER_NAME + ": Removing orphaned file " + fullPath + " from disk.");
					std::filesystem::remove(fullPath);
				}
			}
		}
	}

	void DownloadService::initialize() const
	{
		DownloadServerAppItem dsai;
		nlohmann::json j = dsai;
		AppItem item;
		item.name = SERVER_NAME;
		item.value = j.dump();
		actions.app()->set(item);

		runOrphanedDownloadCleanup();
		actions.download()->reset();
	}

	void DownloadService::run()
	{
		try {
			if (!keepRunning) {
				return;
			}

			spdlog::debug(SERVER_NAME + "::run Download service started.");

			initialize();
			std::string downloadingModelRepo, downloadingFilePath;

			std::thread stopDownloadThread([&]() {
				while (keepRunning) {
					if (downloadingModelRepo.empty() || downloadingFilePath.empty()) {
						std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
						continue;
					}
					if (auto i = actions.download()->get(downloadingModelRepo, downloadingFilePath)) {
						auto &item = i.value();
						if (item.status == DownloadItemStatus::cancelled) {
							spdlog::debug(SERVER_NAME + "::run Stopping downloading of " + item.modelRepo + ": " + item.filePath + "...");
							stopDownload(item);
							spdlog::debug(SERVER_NAME + "::run Stopped downloading of " + item.modelRepo + ": " + item.filePath + ".");
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
				}
			});

			while (keepRunning) {
				updateServerStatus(DownloadServerAppItemStatus::ready);
				spdlog::trace(SERVER_NAME + "::run Checking for queued downloads...");
				if (auto nextItem = actions.download()->getNextQueued()) {
					auto &currentItem = nextItem.value();
					const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

					spdlog::info(SERVER_NAME + "::run Processing download of " + modelName + "...");

					if (currentItem.status == DownloadItemStatus::queued) {
						// Update status to downloading
						currentItem.status = DownloadItemStatus::downloading;
						actions.download()->set(currentItem);
						updateServerStatus(DownloadServerAppItemStatus::preparing, currentItem);

						spdlog::debug(SERVER_NAME + "::run calling startDownload " + modelName + "...");
						try {
							downloadingModelRepo = currentItem.modelRepo;
							downloadingFilePath = currentItem.filePath;
							startDownload(currentItem, true);
							downloadingModelRepo.clear();
							downloadingFilePath.clear();
						} catch (const std::exception &e) {
							spdlog::error(SERVER_NAME + "::run Exception (startDownload): " + std::string(e.what()));
							updateServerStatus(DownloadServerAppItemStatus::error, currentItem, e.what());
						}
						spdlog::info(SERVER_NAME + "::run Download of " + modelName + " complete.");
						updateServerStatus(DownloadServerAppItemStatus::ready);
					}
				}

				runOrphanedDownloadCleanup();

				spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
				std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
			}
			updateServerStatus(DownloadServerAppItemStatus::stopping);
			stopDownloadThread.join();
			spdlog::debug(SERVER_NAME + "::run Download server stopped.");
		} catch (const std::exception &e) {
			spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
			stop();
		}
		updateServerStatus(DownloadServerAppItemStatus::stopped);
	}

	void DownloadService::stop()
	{
		keepRunning = false;
	}

} // namespace wingman::services
