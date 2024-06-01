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

	void Start(const Params &params)
	{
		// TODO: refuse to run if download service is already running
		// const auto status = GetDownloadServiceStatus();
		// if (status != DownloadServiceAppItemStatus::unknown
		// 	&& status != DownloadServiceAppItemStatus::error
		// 	&& status != DownloadServiceAppItemStatus::stopped
		// 	&& status != DownloadServiceAppItemStatus::stopping) {
		// 	std::cerr << "Download service must be stopped before running this tool." << std::endl;
		// 	return;
		// }
		orm::ItemActionsFactory actions;
		clients::DownloadModel(params.model, actions, true, params.force);
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
