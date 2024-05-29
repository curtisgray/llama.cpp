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

	using annoy_index_angular = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;
	using annoy_index_dot_product = Annoy::AnnoyIndex<size_t, float, Annoy::DotProduct, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct Params {
		bool loadAI = false;
		std::string baseInputFilename = "embeddings";
		std::string query = "What is the influence of a base model verses a training model on LoRA?";
		std::string embeddingModel = "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf";
		std::string inferenceModel = "bartowski[-]Meta-Llama-3-8B-Instruct-GGUF[=]Meta-Llama-3-8B-Instruct-Q5_K_S.gguf";
	};

	orm::ItemActionsFactory actions_factory;

	bool singleModel(const Params &params)
	{
		return util::stringCompare(params.embeddingModel, params.inferenceModel);
	}

	// std::optional<std::vector<silk::embedding::Embedding>> getEmbeddings(const silk::embedding::EmbeddingDb &db, const std::vector<std::pair<size_t, float>> &neighbors, const int max = -1)
	// {
	// 	auto ret = std::vector<silk::embedding::Embedding>();
	// 	size_t count = 0;
	// 	if (max == -1) {
	// 		count = neighbors.size();
	// 	} else {
	// 		count = std::min<size_t>(max, neighbors.size());
	// 	}
	// 	for (size_t i = 0; i < count; ++i) {
	// 		const auto &[id, distance] = neighbors[i];
	// 		// Retrieve the data associated with the neighbor index from SQLite
	// 		const auto row = db.getEmbeddingById(static_cast<sqlite3_int64>(id));
	// 		if (row) {
	// 			silk::embedding::Embedding embedding;
	// 			embedding.record = row.value();
	// 			embedding.distance = distance;
	// 			ret.push_back(embedding);
	// 		}
	// 	}
	// 	return ret;
	// }

	std::optional<std::vector<silk::embedding::Embedding>> getEmbeddings(
		const silk::embedding::EmbeddingDb &db,
		const annoy_index_angular &index,
		const nlohmann::json &embedding,
		const int max = -1)
	{
		auto ret = std::vector<silk::embedding::Embedding>();
		size_t count = 0;
		auto queryEmbedding = wingman::silk::embedding::EmbeddingAI::extractEmbeddingFromJson(embedding);

		// Retrieve nearest neighbors
		std::vector<size_t> neighborIndices;
		std::vector<float> distances;
		index.get_nns_by_vector(queryEmbedding.data(), 1000, -1, &neighborIndices, &distances);

		// Create a vector of pairs to store index and distance together
		std::vector<std::pair<size_t, float>> neighbors;
		for (size_t i = 0; i < neighborIndices.size(); ++i) {
			neighbors.emplace_back(neighborIndices[i], distances[i]);
		}

		// Sort the neighbors by distance (ascending order)
		std::sort(neighbors.begin(), neighbors.end(),
			[](const auto &a, const auto &b) { return a.second < b.second; });
		if (max == -1) {
			count = neighbors.size();
		} else {
			count = std::min<size_t>(max, neighbors.size());
		}
		for (size_t i = 0; i < count; ++i) {
			const auto &[id, distance] = neighbors[i];
			// Retrieve the data associated with the neighbor index from SQLite
			const auto row = db.getEmbeddingById(static_cast<sqlite3_int64>(id));
			if (row) {
				silk::embedding::Embedding e;
				e.record = row.value();
				e.distance = distance;
				ret.push_back(e);
			}
		}
		return ret;
	}

	nlohmann::json getSilkContext(const std::vector<silk::embedding::Embedding> &embeddings)
	{
		nlohmann::json silkContext;
		for (const auto &[record, distance] : embeddings) {
			silkContext.push_back({
				{ "id", record.id },
				{ "chunk", record.chunk },
				{ "source", record.source },
				{ "distance", distance }
			});
		}
		return silkContext;
	}

	nlohmann::json getSilkContext(
		const silk::embedding::EmbeddingDb &db,
		const annoy_index_angular &index,
		const nlohmann::json &embedding,
		const int max = -1)
	{
		const auto embeddings = getEmbeddings(db, index, embedding, max);
		if (!embeddings) {
			throw std::runtime_error("Failed to retrieve embeddings");
		}
		return getSilkContext(embeddings.value());
	}

	void printNearestNeighbors(const std::vector<silk::embedding::Embedding> &embeddings)
	{
		// Print the top 10 nearest neighbors
		std::cout << "Top 10 nearest neighbors:" << std::endl;
		for (size_t i = 0; i < std::min<size_t>(10, embeddings.size()); ++i) {
			const auto &[record, distance] = embeddings[i];
			std::cout << "Nearest neighbor " << i << ": Index=" << record.id << ", Angular Distance=" << distance << std::endl;
			std::cout << "   Chunk: " << record.chunk << std::endl;
			std::cout << "   Source: " << record.source << std::endl;
		}
	}

	void Start(Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".db").filename().string()).string();

		int controlPort = 6568;
		int embeddingPort = 6567;
		int inferencePort = 6567;
		if (params.loadAI) {
			controlPort = 45679;
			embeddingPort = 45678;
			inferencePort = 45677;
			disableInferenceLogging = true;
		}
		silk::control::ControlServer controlServer(controlPort, inferencePort);
		silk::embedding::EmbeddingAI embeddingAI(controlPort, embeddingPort, actions_factory);

		if (params.loadAI) {
			controlServer.start();
			if (!controlServer.sendInferenceStartRequest(params.inferenceModel)) {
				throw std::runtime_error("Failed to start inference of control server");
			}
			// wait for 60 seconds while checking for the control server status to become
			//	healthy before starting the embedding AI
			bool isHealthy = false;
			for (int i = 0; i < 60; i++) {
				isHealthy = controlServer.sendInferenceHealthRequest();
				if (isHealthy) {
					break;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			if (!isHealthy) {
				throw std::runtime_error("Inference server is not healthy");
			}
			if (!singleModel(params))
				if (!embeddingAI.start(params.embeddingModel)) {
					throw std::runtime_error("Failed to start embedding AI");
				}
		}

		std::map<std::string, std::string> metadata;
		if (params.loadAI) {
			if (singleModel(params))
				metadata = controlServer.sendRetrieveModelMetadataRequest().value()["metadata"];
			else
				metadata = embeddingAI.ai->getMetadata();
		} else {
			const auto r = embeddingAI.sendRetrieveModelMetadataRequest();
			if (!r) {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
			metadata = r.value()["metadata"];
		}
		int contextSize = 0;
		std::string bosToken;
		std::string eosToken;
		std::string modelName;
		if (metadata.empty())
			throw std::runtime_error("Failed to retrieve model metadata");
		if (metadata.contains("context_length")) {
			contextSize = std::stoi(metadata.at("context_length"));
			std::cout << "Embedding Context size: " << contextSize << std::endl;
		} else {
			throw std::runtime_error("Failed to retrieve model contextSize");
		}
		if (metadata.contains("tokenizer.ggml.bos_token_id")) {
			bosToken = metadata.at("tokenizer.ggml.bos_token_id");
			std::cout << "BOS token: " << bosToken << std::endl;
		} else {
			// throw std::runtime_error("Failed to retrieve model bosToken");
			std::cout << "BOS token not found. Using empty string." << std::endl;
		}
		if (metadata.contains("tokenizer.ggml.eos_token_id")) {
			eosToken = metadata.at("tokenizer.ggml.eos_token_id");
			std::cout << "EOS token: " << eosToken << std::endl;
		} else {
			// throw std::runtime_error("Failed to retrieve model eosToken");
			std::cout << "EOS token not found. Using empty string." << std::endl;
		}

		auto r = embeddingAI.sendRetrieverRequest(bosToken + "Hello world. This is a test." + eosToken);
		if (!r) {
			throw std::runtime_error("Getting dimensions: Failed to retrieve response");
		}
		auto s = silk::embedding::EmbeddingAI::extractEmbeddingFromJson(r.value());
		if (s.empty()) {
			throw std::runtime_error("Getting dimensions: Failed to extract embedding from response");
		}
		std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();

		silk::embedding::EmbeddingDb db(dbPath);
		// annoy_index_dot_product annoyIndex(static_cast<int>(embeddingDimensions));
		annoy_index_angular annoyIndex(static_cast<int>(embeddingDimensions));
		annoyIndex.load(annoyFilePath.c_str());
		std::vector<silk::control::Message> messages;
		messages.emplace_back(silk::control::Message{ "system", "You are a friendly assistant." });
		while (true) {
			printf("\n===========================================\n");
			printf("Enter query (empty to quit): ");
			std::getline(std::cin, params.query);
			if (params.query.empty()) {
				break;
			}
			auto query = util::stringTrim(params.query);
			auto rtrResp = embeddingAI.sendRetrieverRequest(bosToken + query + eosToken);
			if (!rtrResp) {
				throw std::runtime_error("Failed to retrieve response");
			}
			// auto queryEmbedding = wingman::silk::embedding::EmbeddingAI::extractEmbeddingFromJson(rtrResp.value());
			//
			// // Retrieve nearest neighbors
			// std::vector<size_t> neighborIndices;
			// std::vector<float> distances;
			// annoyIndex.get_nns_by_vector(queryEmbedding.data(), 1000, -1, &neighborIndices, &distances);
			//
			// // Create a vector of pairs to store index and distance together
			// std::vector<std::pair<size_t, float>> neighbors;
			// for (size_t i = 0; i < neighborIndices.size(); ++i) {
			// 	neighbors.emplace_back(neighborIndices[i], distances[i]);
			// }
			//
			// // Sort the neighbors by distance (ascending order)
			// std::sort(neighbors.begin(), neighbors.end(),
			// 	[](const auto &a, const auto &b) { return a.second < b.second; });

			// const auto embeddings = getEmbeddings(db, neighbors, 10);

			const auto embeddings = getEmbeddings(db, annoyIndex, rtrResp.value(), 10);

			if (!embeddings) {
				throw std::runtime_error("Failed to retrieve embeddings");
			}
			printNearestNeighbors(embeddings.value());
			nlohmann::json silkContext = getSilkContext(embeddings.value());
			printf("\n===========================================");
			printf("\n===========================================\n");
			// silk::control::OpenAIRequest request;
			// request.model = params.inferenceModel;
			// const auto queryContext = "Context:\n" + silkContext.dump(4) + "\n\n" + query;
			// messages.emplace_back(silk::control::Message{ "user", queryContext });
			// request.messages = nlohmann::json(messages);
			// const auto onChunk = [](const std::string &chunk) {
			// 	std::cout << "Chunk: " << chunk << std::endl;
			// };
			// if (!controlServer.sendChatCompletionRequest(request, onChunk)) {
			// 	// throw std::runtime_error("Failed to send chat completion request");
			// 	std::cerr << "Failed to send chat completion request" << std::endl;
			// }
		}

		if (params.loadAI) {
			embeddingAI.stop();
			controlServer.stop();
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
			} else if (arg == "--embedding-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingModel = argv[i];
			} else if (arg == "--inference-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.inferenceModel = argv[i];
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
	spdlog::set_level(spdlog::level::info);
	auto params = wingman::tools::Params();

	ParseParams(argc, argv, params);
	wingman::tools::Start(params);
}
