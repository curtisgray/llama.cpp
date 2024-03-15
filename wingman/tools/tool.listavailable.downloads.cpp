#include <argparse/argparse.hpp>
#include <fmt/core.h>
// #include <nlohmann/json.hpp>

#include "json.hpp"
#include "orm.h"
#include "curl.h"

namespace wingman::tools {
	void start(const std::optional<std::string> &modelRepo)
	{
		bool found = false;

		if (!modelRepo) {
			orm::ItemActionsFactory actions; // must create an item factory to initialize the download directory needed for the next call
			// display downloaded models
			const auto modelFiles = orm::DownloadItemActions::getModelFiles();
			for (auto &modelFile : modelFiles) {
				found = true;
				const auto itemName = orm::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(modelFile);
				if (!itemName)
					continue;
				const auto din = itemName.value();
				fmt::print("Model: {} ({})\n", din.modelRepo, din.quantization);
			}
		} else if (modelRepo->empty() || modelRepo->find("/") == std::string::npos) {
			// display all models
			const auto models = curl::GetModels();
			for (auto &model : models) {
				const auto &id = model["id"].get<std::string>();
				const auto &name = model["name"].get<std::string>();
				if (modelRepo && !modelRepo->empty()) {
					if (!util::stringContains(id, modelRepo.value(), false))
						continue;
				}
				found = true;
				fmt::print("Model: {} ({})\n", name, curl::HF_MODEL_ENDS_WITH);
				for (auto &[key, value] : model["quantizations"].items()) {
					if (value.size() > 1) {
						fmt::print("\t{} ({} parts)\n", key, value.size());
					} else {
						fmt::print("\t{}\n", key);
					}
				}
			}
		} else {
			// add the trailing HF_MODEL_ENDS_WITH if not present
			std::string modelRepoCopy = modelRepo.value();
			if (!modelRepoCopy.ends_with(curl::HF_MODEL_ENDS_WITH))
				modelRepoCopy.append(curl::HF_MODEL_ENDS_WITH);
			const auto models = curl::GetModelQuantizations(modelRepoCopy);
			for (auto &model : models) {
				const auto &name = model["name"].get<std::string>();
				found = true;
				fmt::print("Model: {} ({})\n", name, curl::HF_MODEL_ENDS_WITH);
				for (auto &[key, value] : model["quantizations"].items()) {
					if (value.size() > 1) {
						fmt::print("\t{} ({} parts)\n", key, value.size());
					} else {
						fmt::print("\t{}\n", key);
					}
				}
			}
		}
		if (!found)
			fmt::print("Nothing found.\n");
	}
}

int main(int argc, char *argv[])
{
	//CLI::App app{ "List available Huggingface.co Llama models. Use --modelRepo [search string] to search for models." };

	argparse::ArgumentParser program("tool.listavailable.downloads", "0.1");

	program
		.add_description("List available Huggingface.co Llama models. Use --modelRepo [search string] to search for models.");

	program.add_argument("-m", "--modelRepo")
		//.required()
		.help("Huggingface model repository name in form '[RepoUser]/[ModelId]'");
	program.add_argument("-a", "--all")
		.default_value(false)
		.implicit_value(true)
		.help("List all available models on Huggingface.co (TheBloke)");

	try {
		program.parse_args(argc, argv);    // Example: ./main --color orange
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(1);
	}

	const auto modelRepo = program.present<std::string>("--modelRepo");

	try {
		wingman::tools::start(modelRepo);
	} catch (const std::exception &e) {
		std::cerr << "Exception: " << std::string(e.what());
		return 1;
	}
	return 0;
}
