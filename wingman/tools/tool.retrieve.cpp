// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include "embedding.h"
#include "exceptions.h"
#include "control.h"
#include "types.h"

namespace wingman::tools {

	using annoy_index = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct Params {
		bool loadAI = false;
		std::string baseInputFilename = "embeddings";
		std::string query = "What is the influence of a base model verses a training model on LoRA?";
		std::string embeddingModel = "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf";
		std::string inferenceModel = "bartowski[-]Meta-Llama-3-8B-Instruct-GGUF[=]Meta-Llama-3-8B-Instruct-Q5_K_S.gguf";
	};

	void Start(Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".db").filename().string()).string();

		int controlPort = 6568;	// TODO: give ingest its own control server
		int embeddingPort = 6567;
		if (params.loadAI) {
			controlPort = 45679;
			embeddingPort = 45678;
			disableInferenceLogging = true;
		}
		silk::control::InferenceAI inferenceAI(controlPort);
		silk::embedding::EmbeddingAI embeddingAI(embeddingPort, controlPort);

		if (params.loadAI) {
			inferenceAI.StartAI(params.inferenceModel);
			embeddingAI.StartAI(params.embeddingModel);
		}

		auto r = embeddingAI.SendRetrieverRequest("Hello world. This is a test.");
		if (!r) {
			throw std::runtime_error("Getting dimensions: Failed to retrieve response");
		}
		auto s = embeddingAI.ExtractEmbeddingFromJson(r.value());
		if (s.empty()) {
			throw std::runtime_error("Getting dimensions: Failed to extract embedding from response");
		}
		std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();

		int contextSize = 0;
		if (params.loadAI) {
			const auto metadata = embeddingAI.ai->getMetadata();
			if (!metadata.empty() && metadata.contains("context_length")) {
				contextSize = std::stoi(metadata.at("context_length"));
			} else {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
		} else {
			r = embeddingAI.SendRetrieveModelMetadataRequest();
			if (!r) {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
			contextSize = std::stoi(r.value()["metadata"]["context_length"].get<std::string>());
			std::cout << "Context size: " << contextSize << std::endl;
		}
		silk::embedding::EmbeddingDb db(dbPath);
		annoy_index annoyIndex(static_cast<int>(embeddingDimensions));
		annoyIndex.load(annoyFilePath.c_str());

		while (true) {
			printf("Enter query (empty to quit): ");
			std::getline(std::cin, params.query);
			if (params.query.empty()) {
				break;
			}
			auto rtrResp = embeddingAI.SendRetrieverRequest(util::stringTrim(params.query));
			if (!rtrResp) {
				throw std::runtime_error("Failed to retrieve response");
			}
			auto queryEmbedding = embeddingAI.ExtractEmbeddingFromJson(rtrResp.value());

			// Retrieve nearest neighbors
			std::vector<size_t> neighborIndices;
			std::vector<float> distances;
			annoyIndex.get_nns_by_vector(queryEmbedding.data(), 10, -1, &neighborIndices, &distances);

			// Print the nearest neighbors
			for (size_t i = 0; i < neighborIndices.size(); ++i) {
				const auto id = neighborIndices[i];
				const auto distance = distances[i];
				// Retrieve the data associated with the neighbor index from SQLite
				const auto row = db.getEmbeddingById(id);
				if (!row) {
					continue;
				}
				std::cout << "Nearest neighbor " << i << ": Index=" << id << ", Distance=" << distance << std::endl;
				std::cout << "   Chunk: " << row->chunk << std::endl;
				std::cout << "   Source: " << row->source << std::endl;
				// You can also print other fields from the retrieved row if needed
			}
			printf("\n===========================================");
			printf("\n===========================================\n");
		}

		if (params.loadAI) {
			embeddingAI.StopAI();
			inferenceAI.StopAI();
		}
	}

	static void ParseParams(int argc, char **argv, Params &params)
	{
		std::string arg;
		bool invalidParam = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--load-ai") {
				params.loadAI = true;
			} else if (arg == "--base-input-name") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.baseInputFilename = argv[i];
			} else if (arg == "--query") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.query = argv[i];
			} else if (arg == "--help" || arg == "-?") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				// std::cout << "  --input-path <path>     Path to the input directory or file" << std::endl;
				std::cout << "  --load-ai                   Load the AI model. Default: false" << std::endl;
				std::cout << "  --base-input-name <name>    Input file base name. Default: embeddings" << std::endl;
				std::cout << "  --query <query>             Query to run against the embeddings. Default: [ask user at runtime]" << std::endl;
				std::cout << "  --help, -?                  Show this help message" << std::endl;
				throw wingman::SilentException();
			} else {
				throw std::runtime_error("unknown argument: " + arg);
			}
		}

		if (invalidParam) {
			throw std::runtime_error("invalid parameter for argument: " + arg);
		}
	}
}


int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);
	auto params = wingman::tools::Params();

	ParseParams(argc, argv, params);
	wingman::tools::Start(params);
}
