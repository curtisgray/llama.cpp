// #include <nlohmann/json.hpp>

#include "json.hpp"
#include "orm.h"
#include "curl.h"

namespace wingman::tools {
	namespace fs = std::filesystem;

	void Start()
	{
		spdlog::info("Dump model data from Huggingface.co to files.");

		const std::string file{ __FILE__ };
		const fs::path directory = fs::path(file).parent_path();
		const auto baseDirectory = directory / fs::path("out");
		fs::create_directories(baseDirectory);

		const auto fullModels = curl::GetRawModels();
		const auto rawModelsOutputPath = baseDirectory / fs::path("raw.models.json");
		std::ofstream ofs(rawModelsOutputPath);
		spdlog::info("Writing {} raw models to {}", fullModels.size(), (rawModelsOutputPath).string());
		ofs << fullModels.dump(4);
		ofs.close();

		const auto models = curl::ParseRawModels(fullModels);
		const auto modelsOutputPath = baseDirectory / fs::path("models.json");
		ofs.open(modelsOutputPath);
		spdlog::info("Writing {} parsed models to {}", models.size(), (modelsOutputPath).string());
		ofs << models.dump(4);
		ofs.close();

		spdlog::info("Success.");
	}
}

int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);

	try {
		wingman::tools::Start();
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("Job's done.");
	return 0;
}
