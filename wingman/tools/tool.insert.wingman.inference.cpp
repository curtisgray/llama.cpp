#include <iostream>
#include <argparse/argparse.hpp>

#include "orm.h"

namespace wingman::tools {
	void start(const std::string &modelAlias, const std::string &modelRepo, const std::string &quantization)
	{
		spdlog::info("Insert wingman inference tool start.");

		// verify that the model exists in the model repo
		orm::ItemActionsFactory actions; // must create an item factory to initialize the download directory needed for the next call
									// actions would normally already be created, but we use this here for the tool
		const auto filePath = orm::DownloadItemActions::getQuantFileNameForModelRepo(modelRepo, quantization);
		if (orm::DownloadItemActions::isDownloaded(modelRepo, filePath, actions.download())) {
			WingmanItem item;
			// the `alias` name is the key, so in order to run multiple copies of a given model, we need to use something other than the model name
			item.alias = modelAlias;
			item.modelRepo = modelRepo;
			item.filePath = filePath;
			item.status = WingmanItemStatus::queued;
			spdlog::info("Queue {}/{}", modelRepo, filePath);
			actions.wingman()->set(item);

			std::cout << modelRepo << " queued for inference." << std::endl;
			spdlog::info("Inserted into db {}:{}", modelRepo, filePath);
		} else {
			std::cout << modelRepo << " not found at " << filePath << std::endl;
		}
	}
}

int main(int argc, char *argv[])
{
	// CLI::App app{ "Download Llama model from Huggingface Wingman models folder." };

	argparse::ArgumentParser program("tool.insert.wingman.inference");

	program.add_description("Schedule to run a Llama model from Huggingface.");
	program.add_argument("--modelRepo")
		.required()
		.help("Huggingface model repository name in form '[RepoUser]/[ModelId]'");
	program.add_argument("--quantization")
		.help("Quantization to infer. Defaults to `Q4_0`");

	try {
		program.parse_args(argc, argv);    // Example: ./main --color orange
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(1);
	}

	const auto modelRepo = program.present<std::string>("--modelRepo");
	const auto quantization = program.present<std::string>("--quantization");

	try {
		//const auto modelRepo = "TheBloke/Amethyst-13B-Mistral";
		//const auto quantization = "Q4_0";
		wingman::tools::start(modelRepo.value(), modelRepo.value(), quantization.value_or("Q4_0"));
	} catch (const std::exception &e) {
		std::cerr << "Exception: " << std::string(e.what());
		return 1;
	}
	return 0;
}
