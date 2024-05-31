#include <iostream>

#include <spdlog/spdlog.h>

#include "exceptions.h"
#include "downloader.h"

namespace wingman::apps {

	struct Params {
		// std::string embeddingModel = "BAAI/bge-large-en-v1.5/bge-large-en-v1.5-Q8_0.gguf";
		// std::string model = "CompendiumLabs/bge-base-en-v1.5-gguf/bge-base-en-v1.5-f16.gguf";
		// std::string model = "MaziyarPanahi/Mistral-7B-Instruct-v0.3-GGUF/Mistral-7B-Instruct-v0.3.Q5_K_S.gguf";
		std::string model;
		bool force;
	};

	// std::atomic requested_shutdown = false;
	// orm::ItemActionsFactory actions_factory;

	// std::function<void(int)> shutdown_handler;
	// void SignalCallback(int signal)
	// {
	// 	shutdown_handler(signal);
	// }

	// DownloadServiceAppItemStatus GetDownloadServiceStatus()
	// {
	// 	auto appItem = actions_factory.app()->get("DownloadService").value_or(AppItem::make("DownloadService"));
	//
	// 	const nlohmann::json j = nlohmann::json::parse(appItem.value);
	// 	const auto downloadServerItem = j.get<DownloadServiceAppItem>();
	// 	return downloadServerItem.status;
	// }

	void Start(const Params &params)
	{
		orm::ItemActionsFactory actions;
		clients::DownloadModel(params.model, actions, true, params.force);
		// TODO: refuse to run if download service is already running
		// const auto status = GetDownloadServiceStatus();
		// if (status != DownloadServiceAppItemStatus::unknown
		// 	&& status != DownloadServiceAppItemStatus::error
		// 	&& status != DownloadServiceAppItemStatus::stopped
		// 	&& status != DownloadServiceAppItemStatus::stopping) {
		// 	std::cerr << "Download service must be stopped before running this tool." << std::endl;
		// 	return;
		// }
		// const auto wingmanHome = GetWingmanHome();
		// const auto [modelRepo, filePath] = silk::ModelLoader::parseModelFromMoniker(params.model);
		// const auto modelPath = orm::DownloadItemActions::getDownloadItemOutputPath(modelRepo, filePath);
		//
		// if (std::filesystem::exists(modelPath)) {
		// 	std::cout << modelRepo << " already exists at " << modelPath << std::endl;
		// 	if (params.force) {
		// 		std::cout << "Deleting existing model at " << modelPath << std::endl;
		// 		std::filesystem::remove(modelPath);
		// 	} else {
		// 		return;
		// 	}
		// }
		// // verify that the model exists on the download server
		// const auto url = orm::DownloadItemActions::urlForModel(modelRepo, filePath);
		// if (curl::RemoteFileExists(url)) {
		// 	// std::cout << "Starting servers..." << std::endl;
		// 	// ensure the file name is only exactly 30 characters long. if longer add ellipsis. if shorter, pad with spaces
		// 	const auto length = modelRepo.length();
		// 	const auto prefixText = length > 30 ? modelRepo.substr(0, 26) + "... " : modelRepo + std::string(30 - length, ' ');
		// 	indicators::ProgressBar fp (
		// 		indicators::option::BarWidth{ 50 },
		// 		indicators::option::Start{ "[" },
		// 		indicators::option::Fill{ "#" },
		// 		indicators::option::Lead{ ">" },
		// 		indicators::option::Remainder{ " " },
		// 		indicators::option::End{ " ]" },
		// 		indicators::option::ForegroundColor{ indicators::Color::yellow },
		// 		indicators::option::ShowElapsedTime{ true },
		// 		indicators::option::ShowRemainingTime{ true },
		// 		indicators::option::PrefixText{ prefixText },
		// 		indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} }
		// 	);
		// 	bool isFirstDownloadUpdate = true;
		// 	auto handler = [&](const wingman::curl::Response *response) -> bool {
		// 		if (isFirstDownloadUpdate) {
		// 			isFirstDownloadUpdate = false;
		// 		}
		// 		const auto percentDownloaded = std::round(100 * static_cast<double>(response->file.totalBytesWritten) / response->file.item->totalBytes);
		// 		fp.set_progress(static_cast<size_t>(percentDownloaded));
		// 		fp.set_option(indicators::option::PostfixText{ fmt::format("{}/{}",util::prettyBytes(response->file.totalBytesWritten),
		// 					util::prettyBytes(response->file.item->totalBytes)) });
		// 		return !requested_shutdown;
		// 	};
		// 	wingman::services::DownloadService service(actions_factory, handler);
		// 	std::thread serviceThread(&wingman::services::DownloadService::run, &service);
		//
		// 	// wait for ctrl-c
		// 	shutdown_handler = [&](int /* signum */) {
		// 		// std::cout << std::endl << "Shutting down ..." << std::endl;
		// 		// if we have received the signal before, abort.
		// 		if (requested_shutdown) abort();
		// 		// First SIGINT recieved, attempt a clean shutdown
		// 		requested_shutdown = true;
		// 		service.stop();
		// 	};
		//
		// 	if (const auto res = std::signal(SIGINT, SignalCallback); res == SIG_ERR) {
		// 		std::cerr << "Failed to register signal handler." << std::endl;
		// 		return;
		// 	}
		//
		// 	std::cout << modelRepo << " found. Scheduling for download..." << std::endl;
		//
		// 	// this is hacky and destructive, but it's the only way rt now without jumping through hoops
		// 	actions_factory.download()->reset();
		// 	const auto active = actions_factory.download()->getAllByStatus(DownloadItemStatus::queued);
		// 	for (const auto &item : active) {
		// 		actions_factory.download()->remove(item.modelRepo, item.filePath);
		// 	}
		//
		// 	const auto result = actions_factory.download()->enqueue(modelRepo, filePath);
		// 	if (!result) {
		// 		std::cerr << "Failed to enqueue download." << std::endl;
		// 		return;
		// 	}
		// 	std::cout << modelRepo << " queued for download." << std::endl;
		//
		// 	// std::cout << std::endl << "Press Ctrl-C to quit" << std::endl;
		//
		// 	// wait for the download to complete
		// 	DownloadServiceAppItemStatus status;
		// 	do {
		// 		status = GetDownloadServiceStatus();
		// 		std::this_thread::sleep_for(std::chrono::seconds(1));
		// 	} while (!requested_shutdown && (status == DownloadServiceAppItemStatus::preparing
		// 		|| status == DownloadServiceAppItemStatus::downloading
		// 		|| status == DownloadServiceAppItemStatus::unknown));
		//
		// 	service.stop();
		// 	serviceThread.join();
		// 	std::cout << std::endl << "Download completed for " << params.model << std::endl;
		// } else {
		// 	std::cout << modelRepo << " not found at " << url << std::endl;
		// }
	}

	static void ParseParams(int argc, char **argv, Params &params)
	{
		std::string arg;
		bool invalidParam = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.model = argv[i];
			} else if (arg == "--force") {
				params.force = true;
			} else if (arg == "--help" || arg == "-?" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  --model <name>              Model moniker to download. Required. '[HuggingFace User]/[Repository Name]/[File Name]'." << std::endl;
				std::cout << "  --force                     Force download even if the model already exists." << std::endl;
				std::cout << "  --help, -?                  Show this help message" << std::endl;
				throw wingman::SilentException();
			} else {
				throw std::runtime_error("unknown argument: " + arg);
			}
		}

		if (invalidParam) {
			throw std::runtime_error("invalid parameter for argument: " + arg);
		}

		if (params.model.empty()) {
			throw std::runtime_error("missing required parameter: --model");
		}
	}
}


int main(int argc, char *argv[])
{
	// disable spdlog logging
	spdlog::set_level(spdlog::level::off);
	auto params = wingman::apps::Params();

	ParseParams(argc, argv, params);
	wingman::apps::Start(params);
}
