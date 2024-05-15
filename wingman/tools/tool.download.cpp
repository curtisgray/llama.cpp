#include <iostream>
#include <argparse/argparse.hpp>

#include "curl.h"
#include "download.service.h"
#include "orm.h"

namespace wingman::tools {
	orm::ItemActionsFactory actions;

	DownloadServiceAppItemStatus GetDownloadServiceStatus()
	{
		auto appItem = actions.app()->get("DownloadService").value_or(AppItem::make("DownloadService"));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		const auto downloadServerItem = j.get<DownloadServiceAppItem>();
		return downloadServerItem.status;
	}

	void Start(const std::string &modelRepo, const std::string &filePath)
	{
		// if (GetDownloadServiceStatus() != DownloadServiceAppItemStatus::stopped) {
		// 	std::cerr << "Download service is already running." << std::endl;
		// 	return;
		// }
		std::cout << "Download tool starting..." << std::endl;
		std::cout <<"Starting download service..." << std::endl;
		// define a lambda function to handle download progress
		const auto onDownloadProgress = [](const curl::Response *response) -> bool {
			fmt::print("{}: {} of {} ({:.1f})             \t\t\t\r",
				response->file.item->filePath,
				util::prettyBytes(response->file.totalBytesWritten),
				util::prettyBytes(response->file.item->totalBytes),
				response->file.item->progress);
			return true;
		};
		services::DownloadService downloadService(actions, onDownloadProgress);
		std::thread downloadServiceThread(&services::DownloadService::run, &downloadService);

		// verify that the model exists on the download server
		const auto url = orm::DownloadItemActions::urlForModel(modelRepo, filePath);
		fmt::print("Schedule download of {}/{}\nFrom {}\n", modelRepo, filePath, url);
		const auto item = actions.download()->enqueue(modelRepo, filePath);
		if (!item) {
			std::cerr << "Failed to schedule download." << std::endl;
			return;
		}
		std::cout << modelRepo << " queued for download." << std::endl;
		// wait for the download to complete
		DownloadServiceAppItemStatus status;
		do {
			status = GetDownloadServiceStatus();
			std::this_thread::sleep_for(std::chrono::seconds(1));
		} while (status == DownloadServiceAppItemStatus::preparing
			|| status == DownloadServiceAppItemStatus::downloading
			|| status == DownloadServiceAppItemStatus::unknown);

		downloadService.stop();
		downloadServiceThread.join();
	}
}

int main(int argc, char *argv[])
{
	// CLI::App app{ "Download Llama model from Huggingface Wingman models folder." };

	argparse::ArgumentParser program("tool.insert.download");

	program.add_description("Schedule to download Llama model from Huggingface to Wingman models folder.");
	program.add_argument("--modelRepo")
		.required()
		.help("Huggingface model repository name in form '[RepoUser]/[ModelId]'");
	program.add_argument("--filePath")
		.required()
		.help("File name from the Huggingface repo to download.");

	try {
		program.parse_args(argc, argv);    // Example: ./main --color orange
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(1);
	}

	const auto modelRepo = program.present<std::string>("--modelRepo");
	const auto filePath = program.present<std::string>("--filePath");

	try {
		if (!modelRepo.has_value() || !filePath.has_value()) {
			std::cerr << "Invalid arguments." << std::endl;
			return 2;
		}
		wingman::tools::Start(modelRepo.value(), filePath.value());
	} catch (const std::exception &e) {
		std::cerr << "Exception: " << std::string(e.what());
		return 1;
	}
	return 0;
}
