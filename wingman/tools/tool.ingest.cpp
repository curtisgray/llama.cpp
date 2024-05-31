// ReSharper disable CppInconsistentNaming
#include <csignal>
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>

#include "ingest.h"
#include "embedding.h"
#include "exceptions.h"
#include "control.h"
#include "embedding.index.h"
#include "progressbar.hpp"
#include "wingman.control.h"

namespace wingman::tools {

	// using annoy_index_angular = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;
	// using annoy_index_dot_product = Annoy::AnnoyIndex<size_t, float, Annoy::DotProduct, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct Params {
		std::string inputPath;
		size_t chunkSize = 0;
		short chunkOverlap = 20;
		bool loadAI = false;
		std::string baseOutputFilename = "embeddings";
		std::string embeddingModel = "BAAI/bge-large-en-v1.5/bge-large-en-v1.5-Q8_0.gguf";
		std::string inferenceModel = "MaziyarPanahi/Mistral-7B-Instruct-v0.3-GGUF/Mistral-7B-Instruct-v0.3.Q5_K_S.gguf";
	};

	std::function<void(int)> shutdown_handler;
	orm::ItemActionsFactory actions_factory;
	bool requested_shutdown;

	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	bool singleModel(const Params &params)
	{
		return util::stringCompare(params.embeddingModel, params.inferenceModel);
	}

	void UnloadAI(const Params& params, silk::embedding::EmbeddingAI& embeddingAI, silk::control::ControlServer& controlServer)
	{
		if (!singleModel(params))
			embeddingAI.stop();
		if (controlServer.isInferenceRunning(params.inferenceModel))
			if (!controlServer.sendInferenceStopRequest(params.inferenceModel))
				std::cerr << "Failed to stop inference of control server" << std::endl;
		controlServer.stop();
	}

	void Start(Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".db").filename().string()).string();

		// delete both files if they exist
		std::filesystem::remove(annoyFilePath);
		std::filesystem::remove(dbPath);

		int controlPort = 6568;
		int embeddingPort = 6567;
		int inferencePort = 6567;
		if (params.loadAI) {
			controlPort = 45679;
			embeddingPort = 45678;
			inferencePort = 45677;
		}
		silk::control::ControlServer controlServer(controlPort, inferencePort);
		silk::embedding::EmbeddingAI embeddingAI(controlPort, embeddingPort, actions_factory);

		// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			requested_shutdown = true;
		};

		if (const auto res = std::signal(SIGINT, SIGINT_Callback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			return;
		}

		if (params.loadAI) {
			controlServer.start();
			if (!controlServer.sendInferenceStartRequest(params.inferenceModel)) {
				throw std::runtime_error("Failed to start inference of control server");
			}
			// wait for 60 seconds while checking for the control server status to become
			//	healthy before starting the embedding AI
			bool isHealthy = false;
			for (int i = 0; i < 60; i++) {
				if (requested_shutdown) {
					break;
				}
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
		if (params.chunkSize == 0)
			params.chunkSize = contextSize;
		std::cout << "Chunk size: " << params.chunkSize << std::endl;

		silk::embedding::EmbeddingDb db(dbPath);

		// annoy_index_dot_product annoyIndex(static_cast<int>(embeddingDimensions));
		// Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy> annoyIndex(static_cast<int>(embeddingDimensions));
		// annoyIndex.on_disk_build(annoyFilePath.c_str());
		silk::embedding::EmbeddingIndex embeddingIndex(params.baseOutputFilename, static_cast<int>(embeddingDimensions));
		embeddingIndex.init();
		progressbar bar(100);

		// Get a list of PDF files in the input path
		std::vector<std::filesystem::path> pdfFiles;
		for (const auto &entry : std::filesystem::directory_iterator(params.inputPath)) {
			if (requested_shutdown) {
				break;
			}
			if (entry.is_regular_file() && entry.path().extension() == ".pdf") {
				pdfFiles.push_back(entry.path());
			}
		}

		auto total_embedding_time = std::chrono::milliseconds(0);

		// pdfFiles.emplace_back(
		// 	R"(C:\Users\curtis.CARVERLAB\source\repos\tt\docs\From Word Models to World Models - Translating from Natural Language to the Probabilistic Language of Thought.pdf)");

		// Process the directory or file...
		disableInferenceLogging = true;

		auto index = 0;
		auto chunkProcessedCount = 0;
		auto chunkOverlap = static_cast<int>(std::ceil(params.chunkOverlap / 100.0 * params.chunkSize));
		std::cout << "Chunk overlap: " << chunkOverlap << "(" << params.chunkOverlap << "%)" << std::endl;
		for (const auto &pdfFilePath : pdfFiles) {
			if (requested_shutdown) {
				break;
			}
			auto start_time = std::chrono::high_resolution_clock::now();
			const auto pdfFile = pdfFilePath.string();
			fmt::print("PDF [{}/{}] {}\n", ++index, pdfFiles.size(), pdfFile);

			const auto chunks = silk::ingestion::ChunkPdfText(pdfFile, params.chunkSize, chunkOverlap);

			if (chunks.empty()) {
				// throw std::runtime_error("Failed to chunk PDF text: " + pdfFile);
				std::cerr << "Failed to chunk PDF text: " << pdfFile << std::endl;
				continue;
			}
			fmt::print("Chunking {} chunks...\n", chunks.size());
			bar.reset();
			bar.set_total(static_cast<int>(chunks.size()) * 3);
			auto chunkIndex = 0;
			for (const auto &chunk : chunks) {
				if (requested_shutdown) {
					break;
				}
				std::string c(chunk);
				c = bosToken + util::stringTrim(c) + eosToken;
				bar.update();
				std::optional<nlohmann::json> rtrResp;
				int maxRetries = 3;
				rtrResp = embeddingAI.sendRetrieverRequest(c);

				if (!rtrResp) {
					std::cerr << std::endl << "Failed to retrieve response. Retrying... retries left: " << maxRetries << std::endl;
					std::cerr << std::endl << "Last chunk index processed was: " << chunkProcessedCount << std::endl;
					bar.update();
					bar.update();
					continue;
				}
				auto storageEmbedding = silk::embedding::EmbeddingAI::extractEmbeddingFromJson(rtrResp.value());

				if (storageEmbedding.empty()) {
					std::cerr << std::endl << "Storage embedding is empty. Possibly found null embeddings from the server." << std::endl;
					bar.update();
					bar.update();
					continue;
				}

				// // Normalize queryEmbedding when using the Annoy::DotProduct index
				// float storageEmbeddingNorm = std::sqrt(silk::embedding::EmbeddingCalc::dotProduct(storageEmbedding));
				// std::transform(storageEmbedding.begin(), storageEmbedding.end(), storageEmbedding.begin(),
				// 			   [storageEmbeddingNorm](const float &c) { return c / storageEmbeddingNorm; });

				if (storageEmbedding.size() != embeddingDimensions) {
					std::cerr << std::endl << "Storage embedding size mismatch. Expected: " << embeddingDimensions << " Got: " << storageEmbedding.size() << std::endl;
					bar.update();
					bar.update();
					continue;
				}
				bar.update();
				// const auto id = db.insertEmbeddingToDb(chunk, pdfFile, storageEmbedding);
				// annoyIndex.add_item(id, storageEmbedding.data());
				embeddingIndex.add(chunk, pdfFile, storageEmbedding);
				bar.update();
				chunkProcessedCount++;
				chunkIndex++;
			}
			std::cout << std::endl;
			auto end_time = std::chrono::high_resolution_clock::now();
			auto embedding_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
			auto etime_minutes = std::chrono::duration_cast<std::chrono::minutes>(embedding_time).count();
			auto etime_seconds = std::chrono::duration_cast<std::chrono::seconds>(embedding_time).count() % 60;
			total_embedding_time += embedding_time;
			auto ttime_minutes = std::chrono::duration_cast<std::chrono::minutes>(total_embedding_time).count();
			auto ttime_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_embedding_time).count() % 60;

			std::cout << "Time to embed PDF: " << etime_minutes << ":" << etime_seconds << std::endl;
			std::cout << "Total embedding time: " << ttime_minutes << ":" << ttime_seconds << std::endl;
		}

		// const auto treeSize = static_cast<int>(embeddingDimensions * 2);
		// std::cout << "Building annoy index of " << treeSize << " trees..." << std::endl;
		// embeddingIndex.build(treeSize);
		std::cout << "Building embedding index of " << embeddingIndex.getTreeSize() << " dimensions..." << std::endl;
		embeddingIndex.build();

		auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_embedding_time).count();
		std::cout << "Total embedding time: " << total_seconds / 60 << ":"
			<< total_seconds % 60 << std::endl;

		if (params.loadAI) {
			UnloadAI(params, embeddingAI, controlServer);
		}
	}

	static void ParseParams(int argc, char **argv, Params &params)
	{
		std::string arg;
		bool invalidParam = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--input-path") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.inputPath = argv[i];
			} else if (arg == "--chunk-size") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.chunkSize = std::stoi(argv[i]);
			} else if (arg == "--chunk-overlap") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.chunkOverlap = std::stoi(argv[i]);
			} else if (arg == "--load-ai") {
				params.loadAI = true;
			} else if (arg == "--base-output-name") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.baseOutputFilename = argv[i];
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
			} else if (arg == "--help" || arg == "-?" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  --input-path <path>         Path to the input directory or file" << std::endl;
				std::cout << "  --chunk-size <size>         Chunk size. Default: [dynamic based on embedding context size]." << std::endl;
				std::cout << "  --chunk-overlap <percent>   Percentage of overlap between chunks. Default: 20" << std::endl;
				std::cout << "  --load-ai                   Load the AI model. Default: false" << std::endl;
				std::cout << "  --base-output-name <name>   Base output file name. Default: embeddings" << std::endl;
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
	spdlog::set_level(spdlog::level::debug);
	auto params = wingman::tools::Params();

	ParseParams(argc, argv, params);
	wingman::tools::Start(params);
}
