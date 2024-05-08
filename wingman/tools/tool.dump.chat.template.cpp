#include <filesystem>

#include "metadata.h"
#include "orm.h"
#include "argparse/argparse.hpp"
#include "spdlog/spdlog.h"

namespace wingman::tools {
	namespace fs = std::filesystem;

	void Start(std::optional<std::string> fullPath)
	{
		spdlog::info("Dump model chat template from a ggml file.");

		// if there's no file path pick the first file in the ~/.wingman/models folder
		if (!fullPath) {
#if defined(_WIN32)
			const auto modelsPath = fs::path(std::getenv("USERPROFILE")) / ".wingman" / "models";
#else
			const auto modelsPath = fs::path(std::getenv("HOME")) / ".wingman" / "models";
#endif
			if (!fs::exists(modelsPath)) {
				spdlog::error("Models folder not found at {}", modelsPath.string());
				return;
			}

			for (const auto &entry : fs::directory_iterator(modelsPath)) {
				if (entry.is_regular_file()) {
					fullPath = entry.path().string();
					break;
				}
			}
		}

		if (!fullPath) {
			spdlog::error("No ggml file found in ~/.wingman/models folder.");
			return;
		}

		// get the base name of the file
		const auto filePath = fs::path(*fullPath).filename().string();
		const auto din = orm::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(filePath);
		if (!din) {
			spdlog::error("Failed to parse download item name from {}", filePath);
			return;
		}
		orm::ItemActionsFactory actions;
		const auto chatTemplate = wingman::GetChatTemplate(din.value().modelRepo, din.value().filePath, actions);
		if (chatTemplate) {
			spdlog::info("Chat template found: {}", chatTemplate->name);
		} else {
			spdlog::info("No chat template found in {}", *fullPath);
		}

		spdlog::info("Success.");
	}
}

int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);
	argparse::ArgumentParser program("tool.dump.chat.template");

	program.add_description("Dump model chat template from a ggml file.");
	program.add_argument("--file")
		// .required()
		.help("Full path to file name to read chat template from.");

	try {
		program.parse_args(argc, argv);    // Example: ./main --color orange
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(1);
	}

	const auto file = program.present<std::string>("--file");

	try {
		wingman::tools::Start(file);
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("Job's done.");
	return 0;
}
