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

	using annoy_index = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct Params {
		std::string inputPath;
		size_t chunkSize = 0;
		bool loadAI = false;
		std::string baseOutputFilename = "embeddings";
		std::string embeddingModel = "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf";
		std::string inferenceModel = "bartowski[-]Meta-Llama-3-8B-Instruct-GGUF[=]Meta-Llama-3-8B-Instruct-Q5_K_S.gguf";
	};

	void Start(Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".db").filename().string()).string();

		// delete both files if they exist
		std::filesystem::remove(annoyFilePath);
		std::filesystem::remove(dbPath);

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
		// std::vector<float> s= silk::ingestion::ExtractEmbeddingFromJson(r.value());
		auto s = embeddingAI.ExtractEmbeddingFromJson(r.value());
		if (s.empty()) {
			throw std::runtime_error("Getting dimensions: Failed to extract embedding from response");
		}
		std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();
		if (params.chunkSize == 0)
			params.chunkSize = embeddingDimensions;

		int contextSize = 0;
		if (params.loadAI) {
			// const auto metadata = ai->getMetadata();
			const auto metadata = embeddingAI.ai->getMetadata();
			if (!metadata.empty() && metadata.contains("context_length")) {
				contextSize = std::stoi(metadata.at("context_length"));
			} else {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
		} else {
			// r = silk::ingestion::SendRetrieveModelMetadataRequest(controlPort);
			r = embeddingAI.SendRetrieveModelMetadataRequest();
			if (!r) {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
			contextSize = std::stoi(r.value()["metadata"]["context_length"].get<std::string>());
			std::cout << "Context size: " << contextSize << std::endl;
		}
		silk::embedding::EmbeddingDb db(dbPath);

		annoy_index annoyIndex(static_cast<int>(embeddingDimensions));
		annoyIndex.on_disk_build(annoyFilePath.c_str());
		progressbar bar(100);

		// Get a list of PDF files in the input path
		std::vector<std::filesystem::path> pdfFiles;
		// for (const auto &entry : std::filesystem::directory_iterator(params.inputPath)) {
		// 	if (entry.is_regular_file() && entry.path().extension() == ".pdf") {
		// 		pdfFiles.push_back(entry.path());
		// 	}
		// }

		auto total_embedding_time = std::chrono::milliseconds(0);

		pdfFiles.emplace_back(
			R"(C:\Users\curtis.CARVERLAB\source\repos\tt\docs\From Word Models to World Models - Translating from Natural Language to the Probabilistic Language of Thought.pdf)");

		// Process the directory or file...
		auto index = 0;
		for (const auto &pdfFilePath : pdfFiles) {
			auto start_time = std::chrono::high_resolution_clock::now();
			const auto pdfFile = pdfFilePath.string();
			std::cout << "PDF [" << ++index << " of " << pdfFiles.size() << "] " << pdfFile << std::endl;
			std::cout << "  Chunking..." << std::endl;
			const auto chunked_data = silk::ingestion::ChunkPdfText(pdfFile, params.chunkSize, contextSize);

			if (!chunked_data) {
				throw std::runtime_error("Failed to chunk PDF text: " + pdfFile);
			}

			// Process the chunked data...
			for (const auto &[pdfName, chunkTypes] : chunked_data.value()) {
				std::cout << "  Ingesting..." << std::endl;
				for (const auto &[chunk_type, chunks] : chunkTypes) {
					if (chunk_type == "page") {
						continue;
					}
					std::cout << "    " << chunk_type << ":" << std::endl;
					bar.reset();
					bar.set_niter(chunks.size() * 3);
					for (const auto &chunk : chunks) {
						std::string c(chunk);
						bar.update();
						std::optional<nlohmann::json> rtrResp;
						int maxRetries = 3;
						do {
							rtrResp = embeddingAI.SendRetrieverRequest(util::stringTrim(c));
							if (!rtrResp) {
								std::cerr << "Failed to retrieve response. Retrying... retries left: " << maxRetries << std::endl;
							} else {
								break;
							}
							// pause for a bit
							std::this_thread::sleep_for(std::chrono::milliseconds(1000));
						} while (!rtrResp && --maxRetries > 0);
						if (!rtrResp) {
							throw std::runtime_error("Failed to retrieve response");
						}
						auto storageEmbedding = embeddingAI.ExtractEmbeddingFromJson(rtrResp.value());
						if (storageEmbedding.size() != embeddingDimensions) {
							bar.update();
							bar.update();
							continue;
						}
						// std::cout << "*";
						bar.update();
						const auto id = db.insertEmbeddingToDb(chunk, pdfName, storageEmbedding);
						annoyIndex.add_item(id, storageEmbedding.data());
						// std::cout << ".";
						bar.update();
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
	spdlog::set_level(spdlog::level::trace);
	auto params = wingman::tools::Params();

	ParseParams(argc, argv, params);
	wingman::tools::Start(params);
}
