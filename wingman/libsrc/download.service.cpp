#include <chrono>
#include <thread>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "json.hpp"
#include "download.service.h"
#include "metadata.h"

namespace wingman::services {
	DownloadService::DownloadService(orm::ItemActionsFactory &actionsFactory
			, const std::function<bool(curl::Response *)> &onDownloadProgress
	)
		: actions(actionsFactory)
		, onDownloadProgress(onDownloadProgress)
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
		updateServerStatus(DownloadServiceAppItemStatus::downloading, downloadItem);
		const auto response = Fetch(request);
	}

	void DownloadService::stopDownload(const DownloadItem &downloadItem)
	{
		keepDownloading = false;
	}

	void DownloadService::updateServerStatus(const DownloadServiceAppItemStatus &status, std::optional<DownloadItem> downloadItem, std::optional<std::string> error)
	{
		auto appItem = actions.app()->get(SERVER_NAME).value_or(AppItem::make(SERVER_NAME));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto downloadServerItem = j.get<DownloadServiceAppItem>();
		downloadServerItem.status = status;
		if (error) {
			downloadServerItem.error = error;
		}
		if (downloadItem) {
			downloadServerItem.currentDownload.emplace(downloadItem.value());
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
		DownloadServiceAppItem dsai;
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

			updateServerStatus(DownloadServiceAppItemStatus::ready);
			while (keepRunning) {
				spdlog::trace(SERVER_NAME + "::run Checking for queued downloads...");
				if (auto nextItem = actions.download()->getNextQueued()) {
					auto &currentItem = nextItem.value();
					const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

					spdlog::info(SERVER_NAME + "::run Processing download of " + modelName + "...");

					if (currentItem.status == DownloadItemStatus::queued) {
						// Update status to downloading
						currentItem.status = DownloadItemStatus::downloading;
						actions.download()->set(currentItem);
						updateServerStatus(DownloadServiceAppItemStatus::preparing, currentItem);

						spdlog::debug(SERVER_NAME + "::run calling startDownload " + modelName + "...");
						try {
							downloadingModelRepo = currentItem.modelRepo;
							downloadingFilePath = currentItem.filePath;
							startDownload(currentItem, true);
							// Generate and save metadata
							spdlog::debug(SERVER_NAME + "::run Extracting metadata from " + modelName + "...");
							const auto metadata = GetModelMetadata(currentItem.modelRepo, currentItem.filePath, actions);
							if (metadata) {
								currentItem.metadata = metadata.value().dump();
								actions.download()->set(currentItem);
								spdlog::debug(SERVER_NAME + "::run Metadata extracted from " + modelName + ".");
							} else {
								spdlog::warn(SERVER_NAME + "::run Metadata not found for " + modelName + ".");
							}
							const auto chatTemplate = GetChatTemplate(currentItem.modelRepo, currentItem.filePath, actions);
							if (chatTemplate) {
								spdlog::debug("{}::run Chat template '{}' extracted from {}", SERVER_NAME, chatTemplate.value().name, modelName);
							} else {
								spdlog::warn("{}::run Chat template not found for {}", SERVER_NAME, modelName);
							}
							downloadingModelRepo.clear();
							downloadingFilePath.clear();
						} catch (const std::exception &e) {
							spdlog::error(SERVER_NAME + "::run Exception (startDownload): " + std::string(e.what()));
							updateServerStatus(DownloadServiceAppItemStatus::error, currentItem, e.what());
						}
						spdlog::info(SERVER_NAME + "::run Download of " + modelName + " complete.");
						updateServerStatus(DownloadServiceAppItemStatus::ready);
					}
				}

				runOrphanedDownloadCleanup();

				spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
				std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
			}
			updateServerStatus(DownloadServiceAppItemStatus::stopping);
			stopDownloadThread.join();
			spdlog::debug(SERVER_NAME + "::run Download server stopped.");
		} catch (const std::exception &e) {
			spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
			stop();
		}
		updateServerStatus(DownloadServiceAppItemStatus::stopped);
	}

	void DownloadService::stop()
	{
		spdlog::debug(SERVER_NAME + "::stop Download service stopping...");
		keepRunning = false;
	}

} // namespace wingman::services
