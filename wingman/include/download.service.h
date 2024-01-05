#pragma once

#include "types.h"
#include "orm.h"
#include "curl.h"

namespace wingman::services {

	class DownloadService {
		std::atomic<bool> keepRunning = true;

		orm::ItemActionsFactory &actions;
		const std::string SERVER_NAME = "DownloadService";
		const int QUEUE_CHECK_INTERVAL = 1000; // Assuming 1000ms as in TypeScript

		void startDownload(const DownloadItem &downloadItem, bool overwrite);

		void stopDownload(const DownloadItem &downloadItem);

		void updateServerStatus(const DownloadServiceAppItemStatus &status, std::optional<DownloadItem> downloadItem = std::nullopt,
			std::optional<std::string> error = std::nullopt);

		void runOrphanedDownloadCleanup() const;

		void initialize() const;

		std::function<bool(curl::Response *)> onDownloadProgress = nullptr;
		//std::function<bool(DownloadServiceAppItem *)> onServiceStatus = nullptr;

		std::atomic<bool> keepDownloading = true;

	public:
		DownloadService(orm::ItemActionsFactory &actionsFactory
			, const std::function<bool(curl::Response *)> &onDownloadProgress = nullptr
			//, const std::function<bool(DownloadServiceAppItem *)> &onServiceStatus = nullptr
		);

		void run();

		void stop();

	};

} // namespace wingman::services
