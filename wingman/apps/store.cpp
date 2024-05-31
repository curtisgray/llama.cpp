// ReSharper disable CppInconsistentNaming
#include <csignal>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

#include "ingest.h"
#include "embedding.h"
#include "exceptions.h"
#include "control.h"
#include "downloader.h"
#include "embedding.index.h"
#include "wingman.control.h"
#include "spdlog/spdlog.h"

namespace wingman::apps {

	struct Params {
		std::string inputPath;
		size_t chunkSize = 0;
		short chunkOverlap = 20;
		int embeddingPort = 45678;
		std::string memoryBankName = "embeddings";
		// std::string embeddingModel = "BAAI/bge-large-en-v1.5/bge-large-en-v1.5-Q8_0.gguf";
		std::string embeddingModel = "CompendiumLabs/bge-base-en-v1.5-gguf/bge-base-en-v1.5-f16.gguf";
	};

	std::function<void(int)> shutdown_handler;
	bool requested_shutdown;

	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	void Start(Params &params)
	{
		orm::ItemActionsFactory actions;
		clients::DownloaderResult res = clients::DownloadModel(params.embeddingModel, actions, true, false);

		const auto wingmanHome = GetWingmanHome();

		const auto annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.memoryBankName + ".ann").filename().string()).string();
		const auto dbPath = (wingmanHome / "data" / std::filesystem::path(params.memoryBankName + ".db").filename().string()).string();
		std::filesystem::remove(annoyFilePath);
		std::filesystem::remove(dbPath);
		silk::embedding::EmbeddingAI embeddingAI(params.embeddingPort, actions);

		// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			requested_shutdown = true;
		};

		if (const auto res = std::signal(SIGINT, SIGINT_Callback); res == SIG_ERR) {
			std::cerr << " (start) Failed to register signal handler.";
			return;
		}

		if (!embeddingAI.start(params.embeddingModel)) {
			throw std::runtime_error("Failed to start embedding AI");
		}
		disableInferenceLogging = true;
		auto metadata = embeddingAI.ai->getMetadata();
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
			std::cout << "BOS token not found. Using empty string." << std::endl;
		}
		if (metadata.contains("tokenizer.ggml.eos_token_id")) {
			eosToken = metadata.at("tokenizer.ggml.eos_token_id");
			std::cout << "EOS token: " << eosToken << std::endl;
		} else {
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

		auto chunkOverlap = static_cast<int>(std::ceil(params.chunkOverlap / 100.0 * params.chunkSize));
		std::cout << "Chunk overlap: " << chunkOverlap << " (" << params.chunkOverlap << "%)" << std::endl;
		std::cout << "Memory bank: " << params.memoryBankName << std::endl;
		std::cout << std::endl;

		silk::embedding::EmbeddingIndex embeddingIndex(params.memoryBankName, static_cast<int>(embeddingDimensions));
		embeddingIndex.init();

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

		// pdfFiles.emplace_back(
		// 	R"(C:\Users\curtis.CARVERLAB\source\repos\tt\docs\From Word Models to World Models - Translating from Natural Language to the Probabilistic Language of Thought.pdf)");

		// Process the directory or file...
		try {
			auto chunkProcessedCount = 0;
			auto pdfIndex = 1;

			// indicators::DynamicProgress<indicators::ProgressBar> bars;
			// bars.set_option(indicators::option::HideBarWhenComplete{ true });
			//
			// indicators::ProgressBar fp(indicators::option::BarWidth{ 50 },
			//                            indicators::option::Start{ "[" },
			//                            indicators::option::Fill{ "#" },
			//                            indicators::option::Lead{ ">" },
			//                            indicators::option::Remainder{ " " },
			//                            indicators::option::End{ " ]" },
			//                            indicators::option::ForegroundColor{ indicators::Color::yellow },
			//                            indicators::option::ShowElapsedTime{ true },
			//                            indicators::option::ShowRemainingTime{ true },
			//                            indicators::option::PrefixText{ "File  " },
			//                            indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} },
			//                            indicators::option::MaxProgress{ pdfFiles.size() }
			// );
			// auto pfp = std::make_shared<indicators::ProgressBar>(
			// 	indicators::option::BarWidth{ 50 },
			// 	indicators::option::Start{ "[" },
			// 	indicators::option::Fill{ "#" },
			// 	indicators::option::Lead{ ">" },
			// 	indicators::option::Remainder{ " " },
			// 	indicators::option::End{ " ]" },
			// 	indicators::option::ForegroundColor{ indicators::Color::yellow },
			// 	indicators::option::ShowElapsedTime{ true },
			// 	indicators::option::ShowRemainingTime{ true },
			// 	indicators::option::PrefixText{ "File  " },
			// 	indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} },
			// 	indicators::option::MaxProgress{ pdfFiles.size() }
			// );
			// const auto pdfBarIndex = bars.push_back(*pfp);
			// const auto pdfBarIndex = bars.push_back(fp);
			auto start_time = std::chrono::high_resolution_clock::now();
			for (const auto &pdfFilePath : pdfFiles) {
				if (requested_shutdown) {
					break;
				}
				const auto pdfFile = pdfFilePath.string();
				// get the file name from the path
				const auto pdfFileName = pdfFilePath.filename().string();
				// ensure the file name is only 30 characters long. if longer add ellipsis
				constexpr auto displayNameLength = 60;
				const auto pdfFileDisplayName = pdfFileName.size() > displayNameLength ? pdfFileName.substr(0, displayNameLength - 4) + "..." : pdfFileName;
				// bars[pdfBarIndex].set_option(indicators::option::PostfixText{ fmt::format("{}/{} {}", pdfIndex, pdfFiles.size(), pdfFileDisplayName) });
				std::cout << "Processing " << fmt::format("{}/{} {}", pdfIndex, pdfFiles.size(), pdfFileDisplayName) << std::endl;

				const auto chunks = silk::ingestion::ChunkPdfText(pdfFile, params.chunkSize, chunkOverlap);

				if (!chunks.empty()) {
					auto chunkIndex = 1;
					indicators::ProgressBar p(indicators::option::BarWidth{ 50 },
											                           indicators::option::Start{ "[" },
											                           indicators::option::Fill{ "=" },
											                           indicators::option::Lead{ ">" },
											                           indicators::option::Remainder{ " " },
											                           indicators::option::End{ " ]" },
											                           indicators::option::ForegroundColor{ indicators::Color::cyan },
											                           indicators::option::ShowElapsedTime{ true },
											                           indicators::option::ShowRemainingTime{ true },
											                           indicators::option::PrefixText{ "Chunk " },
											                           indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} },
											                           indicators::option::MaxProgress{ chunks.size() }
										);
					// auto cp = std::make_shared<indicators::ProgressBar>(
					// 	indicators::option::BarWidth{ 50 },
					// 	indicators::option::Start{ "[" },
					// 	indicators::option::Fill{ "=" },
					// 	indicators::option::Lead{ ">" },
					// 	indicators::option::Remainder{ " " },
					// 	indicators::option::End{ " ]" },
					// 	indicators::option::ForegroundColor{ indicators::Color::cyan },
					// 	indicators::option::ShowElapsedTime{ true },
					// 	indicators::option::ShowRemainingTime{ true },
					// 	indicators::option::PrefixText{ "Chunk " },
					// 	indicators::option::FontStyles{ std::vector<indicators::FontStyle>{indicators::FontStyle::bold} },
					// 	indicators::option::MaxProgress{ chunks.size() }
					// );
					// const auto chunkBarIndex = bars.push_back(*cp);
					// const auto chunkBarIndex = bars.push_back(p);
					for (const auto &chunk : chunks) {
						if (requested_shutdown) {
							break;
						}

						// bars[chunkBarIndex].set_option(indicators::option::PostfixText{ fmt::format("{}/{}", chunkIndex, chunks.size()) });
						p.set_option(indicators::option::PostfixText{ fmt::format("{}/{}", chunkIndex, chunks.size()) });

						std::string embeddingString = chunk;
						std::string c = bosToken;
						c += util::stringTrim(embeddingString);
						c += eosToken;
						std::optional<nlohmann::json> rtrResp = embeddingAI.sendRetrieverRequest(c);

						if (rtrResp) {
							auto storageEmbedding = silk::embedding::EmbeddingAI::extractEmbeddingFromJson(rtrResp.value());

							if (!storageEmbedding.empty()) {
								if (storageEmbedding.size() == embeddingDimensions) {
									embeddingIndex.add(embeddingString, pdfFile, storageEmbedding);
								}
							}
						}
						chunkProcessedCount++;
						chunkIndex++;
						// bars[chunkBarIndex].tick();
						p.tick();
					}
				}
				pdfIndex++;
				// bars[pdfBarIndex].tick();
			}

			std::cout << std::endl << "Building embedding index of with a tree size of " << embeddingIndex.getTreeSize() << " nodes..." << std::endl;
			embeddingIndex.build();

			auto end_time = std::chrono::high_resolution_clock::now();
			auto embedding_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

			auto hours = embedding_time / std::chrono::hours(1);
			embedding_time %= std::chrono::hours(1);
			auto minutes = embedding_time / std::chrono::minutes(1);
			embedding_time %= std::chrono::minutes(1);
			auto seconds = embedding_time.count();
			// std::cout << "Total embedding time: " << total_seconds / 60 << ":"
			// 	<< total_seconds % 60 << std::endl;
			std::cout << "Total embedding time: " << hours << "h " << minutes << "m " << seconds << "s" << std::endl;
			embeddingAI.stop();
		} catch (const std::exception &e) {
			std::cerr << "Error: " << e.what() << std::endl;
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
			} else if (arg == "--port") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingPort = std::stoi(argv[i]);
			} else if (arg == "--memory-bank") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.memoryBankName = argv[i];
			} else if (arg == "--embedding-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingModel = argv[i];
			} else if (arg == "--help" || arg == "-?" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  --input-path <path>         Path to the input directory or file. Required." << std::endl;
				std::cout << "  --chunk-size <size>         Chunk size. Default: [dynamic based on embedding context size]." << std::endl;
				std::cout << "  --chunk-overlap <percent>   Percentage of overlap between chunks. Default: 20." << std::endl;
				std::cout << "  --memory-bank <name>        Output file base name. Default: embeddings." << std::endl;
				std::cout << "  --help, -?                  Show this help message." << std::endl;
				throw wingman::SilentException();
			} else {
				throw std::runtime_error("unknown argument: " + arg);
			}
		}

		if (invalidParam) {
			throw std::runtime_error("invalid parameter for argument: " + arg);
		}
		if (params.inputPath.empty()) {
			throw std::runtime_error("Input path is required.");
		}
	}
}

int main(int argc, char *argv[])
{
	// disable spdlog logging
	spdlog::set_level(spdlog::level::off);
	auto params = wingman::apps::Params();

	ParseParams(argc, argv, params);
	indicators::show_console_cursor(false);
	wingman::apps::Start(params);
	indicators::show_console_cursor(true);
}
