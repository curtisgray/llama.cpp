// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include "ingest.h"
#include "embedding.h"
#include "exceptions.h"
#include "control.h"
#include "progressbar.hpp"

namespace wingman::tools {

	using annoy_index_angular = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;
	using annoy_index_dot_product = Annoy::AnnoyIndex<size_t, float, Annoy::DotProduct, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct Params {
		std::string inputPath;
		size_t chunkSize = 0;
		bool loadAI = false;
		std::string baseOutputFilename = "embeddings";
		std::string embeddingModel = "second-state/All-MiniLM-L6-v2-Embedding-GGUF/all-MiniLM-L6-v2-Q5_K_M.gguf";
		std::string inferenceModel = "bartowski[-]Meta-Llama-3-8B-Instruct-GGUF[=]Meta-Llama-3-8B-Instruct-Q5_K_S.gguf";
	};

	orm::ItemActionsFactory actions_factory;

	bool singleModel(const Params& params)
	{
		return util::stringCompare(params.embeddingModel, params.inferenceModel);
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
				embeddingAI.start(params.embeddingModel);
		}

		auto r = embeddingAI.sendRetrieverRequest("Hello world. This is a test.");
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
			params.chunkSize = embeddingDimensions;

		int contextSize = 0;
		if (params.loadAI) {
			std::map<std::string, std::string> metadata;
			if (singleModel(params))
				metadata = embeddingAI.ai->getMetadata();
			else
				metadata = controlServer.sendRetrieveModelMetadataRequest().value()["metadata"];
			if (!metadata.empty() && metadata.contains("context_length")) {
				contextSize = std::stoi(metadata.at("context_length"));
				std::cout << "Embedding Context size: " << contextSize << std::endl;
			} else {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
		} else {
			r = embeddingAI.sendRetrieveModelMetadataRequest();
			if (!r) {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
			contextSize = std::stoi(r.value()["metadata"]["context_length"].get<std::string>());
			std::cout << "Context size: " << contextSize << std::endl;
		}
		silk::embedding::EmbeddingDb db(dbPath);

		// annoy_index_angular annoyIndex(static_cast<int>(embeddingDimensions));
		annoy_index_dot_product annoyIndex(static_cast<int>(embeddingDimensions));
		annoyIndex.on_disk_build(annoyFilePath.c_str());
		progressbar bar(100);

		// Get a list of PDF files in the input path
		std::vector<std::filesystem::path> pdfFiles;
		for (const auto &entry : std::filesystem::directory_iterator(params.inputPath)) {
			if (entry.is_regular_file() && entry.path().extension() == ".pdf") {
				pdfFiles.push_back(entry.path());
			}
		}

		auto total_embedding_time = std::chrono::milliseconds(0);

		// pdfFiles.emplace_back(
		// 	R"(C:\Users\curtis.CARVERLAB\source\repos\tt\docs\From Word Models to World Models - Translating from Natural Language to the Probabilistic Language of Thought.pdf)");

		// Process the directory or file...
		auto index = 0;
		auto chunkProcessedCount = 0;
		for (const auto &pdfFilePath : pdfFiles) {
			auto start_time = std::chrono::high_resolution_clock::now();
			const auto pdfFile = pdfFilePath.string();
			std::cout << "PDF [" << ++index << " of " << pdfFiles.size() << "] " << pdfFile << std::endl;
			std::cout << "  Chunking..." << std::endl;
			const auto chunked_data = silk::ingestion::ChunkPdfText(pdfFile, params.chunkSize, contextSize);

			if (!chunked_data) {
				throw std::runtime_error("Failed to chunk PDF text: " + pdfFile);
			}
			// count how many chunks are in the chunked_data
			size_t chunkCount = 0;
			for (const auto &[pdfName, chunkTypes] : chunked_data.value()) {
				for (const auto &[chunk_type, chunks] : chunkTypes) {
					chunkCount += chunks.size();
				}
			}
			// this code apparantly does the same thing as the above code, but yeesh!
			// size_t chunkCount = std::accumulate(
			// 	chunked_data.value().begin(), chunked_data.value().end(), 0ULL,
			// 	[](size_t acc, const auto &p) {
			// 		return acc + std::accumulate(
			// 			p.second.begin(), p.second.end(), 0ULL,
			// 			[](size_t ia, const auto &ct) {
			// 				return ia + ct.second.size();
			// 		});
			// });
			std::cout << "  Chunks in this PDF: " << chunkCount << std::endl;
			// Process the chunked data...
			for (const auto &[pdfName, chunkTypes] : chunked_data.value()) {
				std::cout << "  Ingesting..." << std::endl;
				for (const auto &[chunk_type, chunks] : chunkTypes) {
					if (chunk_type == "page") {
						continue;
					}
					std::cout << "    " << chunk_type << " (" << chunks.size() << "):" << std::endl;
					bar.reset();
					bar.set_niter(static_cast<int>(chunks.size()) * 3);

					for (const auto &chunk : chunks) {
						std::string c(chunk);
						bar.update();
						std::optional<nlohmann::json> rtrResp;
						int maxRetries = 3;
						bool triedRestartingEmbeddingAI = false;
						do {
							rtrResp = embeddingAI.sendRetrieverRequest(util::stringTrim(c));
							if (!rtrResp) {
								// if (triedRestartingEmbeddingAI) {
									std::cerr << std::endl << "Failed to retrieve response. Retrying... retries left: " << maxRetries << std::endl;
									std::cerr << std::endl << "Last chunk processed before failure was: " << chunkProcessedCount << std::endl;
								// } else {
								// 	std::cerr << std::endl << "Failed to retrieve response. Restarting the embedding AI..." << std::endl;
								// 	triedRestartingEmbeddingAI = true;
								// 	embeddingAI.stop();
								// 	// sleeping for a bit to allow the AI to stop
								// 	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
								// 	embeddingAI.start(params.embeddingModel);
								// }
							} else {
								break;
							}
							// pause for a bit
							std::this_thread::sleep_for(std::chrono::milliseconds(3000));
						} while (!rtrResp && --maxRetries > 0);

						if (!rtrResp) {
							std::cerr << std::endl << "Last chunk processed was: " << chunkProcessedCount << std::endl;
							throw std::runtime_error("Failed to retrieve response");
						}
						auto storageEmbedding = silk::embedding::EmbeddingAI::extractEmbeddingFromJson(rtrResp.value());

						if (storageEmbedding.empty()) {
							// std::cerr << std::endl << "Storage embedding is empty. Possibly found null embeddings from the server." << std::endl;
							bar.update(false);
							bar.update(false);
							continue;
						}

						// Normalize queryEmbedding when using the Annoy::DotProduct index
						float storageEmbeddingNorm = std::sqrt(silk::embedding::EmbeddingCalc::dotProduct(storageEmbedding));
						std::transform(storageEmbedding.begin(), storageEmbedding.end(), storageEmbedding.begin(),
									   [storageEmbeddingNorm](const float &c) { return c / storageEmbeddingNorm; });

						if (storageEmbedding.size() != embeddingDimensions) {
							bar.update(false);
							bar.update(false);
							continue;
						}
						// std::cout << "*";
						bar.update();
						const auto id = db.insertEmbeddingToDb(chunk, pdfName, storageEmbedding);
						annoyIndex.add_item(id, storageEmbedding.data());
						// std::cout << ".";
						bar.update();
						chunkProcessedCount++;
					}
					std::cout << std::endl;
				}
			}
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

		// const auto treeSize = static_cast<int>(embeddingDimensions * embeddingDimensions);
		const auto treeSize = static_cast<int>(embeddingDimensions);
		std::cout << "Building annoy index of " << treeSize << " trees..." << std::endl;
		annoyIndex.build(treeSize);

		auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_embedding_time).count();
		std::cout << "Total embedding time: " << total_seconds / 60 << ":"
			<< total_seconds % 60 << std::endl;

		if (params.loadAI) {
			if (!singleModel(params))
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
			if (arg == "--input-path") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.inputPath = argv[i];
			} else if (arg == "chunk-size") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.chunkSize = std::stoi(argv[i]);
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
			} else if (arg == "--help" || arg == "-?") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  --input-path <path>         Path to the input directory or file" << std::endl;
				std::cout << "  --chunk-size <size>         Chunk size. Default: [dynamic based on embedding dimensions]." << std::endl;
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
