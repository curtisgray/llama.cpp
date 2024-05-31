#include "downloader.h"

#include <thread>

#include <spdlog/spdlog.h>
#include <indicators/font_style.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/setting.hpp>

#include "download.service.h"
#include "embedding.index.h"
#include "types.h"

namespace wingman::clients {

	DownloadServiceAppItemStatus GetDownloadServiceStatus(orm::ItemActionsFactory &actions)
	{
		auto appItem = actions.app()->get("DownloadService").value_or(AppItem::make("DownloadService"));

		const nlohmann::json j = nlohmann::json::parse(appItem.value);
		const auto downloadServerItem = j.get<DownloadServiceAppItem>();
		return downloadServerItem.status;
	}

	DownloaderResult DownloadModel(const std::string &modelMoniker, orm::ItemActionsFactory& actions, bool showProgress, bool force)
	{
		const auto wingmanHome = GetWingmanHome();
		std::string modelRepo, filePath;
		try {
			std::tie(modelRepo, filePath) = silk::ModelLoader::parseModelFromMoniker(modelMoniker);
		} catch (const std::exception &e) {
			if (showProgress)
				std::cerr << "Failed to parse model moniker: " << e.what() << std::endl;
			return DownloaderResult::bad_model_moniker;
		}
		const auto modelPath = orm::DownloadItemActions::getDownloadItemOutputPath(modelRepo, filePath);

		if (std::filesystem::exists(modelPath)) {
			if (force) {
				if (showProgress)
					std::cout << "Removing existing model at " << modelPath << std::endl;
				std::filesystem::remove(modelPath);
			} else {
				if (showProgress)
					std::cout << modelRepo << " already exists at " << modelPath << std::endl;
				return DownloaderResult::already_exists;
			}
		}

		if (showProgress)
			std::cout << "Verifying model is available for download..." << std::endl;
		// verify that the model exists on the download server
		const auto url = orm::DownloadItemActions::urlForModel(modelRepo, filePath);
		if (curl::RemoteFileExists(url)) {
			// ensure the file name is only exactly 30 characters long. if longer add ellipsis. if shorter, pad with spaces

			const auto length = modelRepo.length();
			const auto prefixText = length > 30 ? modelRepo.substr(0, 26) + "... " : modelRepo + std::string(30 - length, ' ');
			indicators::ProgressBar fp(
				indicators::option::BarWidth{ 50 },
				indicators::option::Start{ "[" },
				indicators::option::Fill{ "#" },
				indicators::option::Lead{ ">" },
				indicators::option::Remainder{ " " },
				indicators::option::End{ " ]" },
				indicators::option::ForegroundColor{ indicators::Color::yellow },
				indicators::option::ShowElapsedTime{ true },
				indicators::option::ShowRemainingTime{ true },
				indicators::option::PrefixText{ prefixText },
				indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} }
			);
			auto handler = [&](const wingman::curl::Response *response) -> bool {
				if (showProgress) {
					const auto percentDownloaded = std::round(100 * static_cast<double>(response->file.totalBytesWritten) / response->file.item->totalBytes);
					fp.set_progress(static_cast<size_t>(percentDownloaded));
					fp.set_option(indicators::option::PostfixText{ fmt::format("{}/{}",util::prettyBytes(response->file.totalBytesWritten),
								util::prettyBytes(response->file.item->totalBytes)) });
				}
				return true;
			};
			wingman::services::DownloadService service(actions, handler);
			std::thread serviceThread(&wingman::services::DownloadService::run, &service);

			if (showProgress)
				std::cout << modelRepo << " found in remote repository. Scheduling for download..." << std::endl;
			// this is hacky and destructive, but it's the only way rt now without jumping through hoops
			actions.download()->reset();
			const auto active = actions.download()->getAllByStatus(DownloadItemStatus::queued);
			for (const auto &item : active) {
				actions.download()->remove(item.modelRepo, item.filePath);
			}

			const auto result = actions.download()->enqueue(modelRepo, filePath);
			if (!result) {
				return DownloaderResult::failed;
			}
			if (showProgress)
				std::cout << modelRepo << " queued for download." << std::endl;

			// wait for the download to complete
			DownloadServiceAppItemStatus status;
			do {
				status = GetDownloadServiceStatus(actions);
				std::this_thread::sleep_for(std::chrono::seconds(1));
			} while (status == DownloadServiceAppItemStatus::preparing
				|| status == DownloadServiceAppItemStatus::downloading
				|| status == DownloadServiceAppItemStatus::unknown);

			if (showProgress)
				std::cout << std::endl << "Download completed for " << modelMoniker << std::endl;

			service.stop();
			serviceThread.join();
			return DownloaderResult::success;
		}
		if (showProgress)
			std::cerr << modelRepo << " not found at " << url << std::endl;
		return DownloaderResult::bad_url;
	}
}
