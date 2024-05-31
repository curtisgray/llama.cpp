#include <iostream>
#include <thread>

#include <csignal>

#include "exceptions.h"
#include "control.h"
#include "downloader.h"
#include "embedding.index.h"
#include "types.h"
#include "spdlog/spdlog.h"

namespace wingman::apps {

	struct Params {
		std::string memoryBankName = "embeddings";
		std::string query;
		// std::string embeddingModel = "BAAI/bge-large-en-v1.5/bge-large-en-v1.5-Q8_0.gguf";
		std::string embeddingModel = "CompendiumLabs/bge-base-en-v1.5-gguf/bge-base-en-v1.5-f16.gguf";
		int embeddingPort = 45678;
		bool jsonOutput = false;
	};

	std::function<void(int)> shutdown_handler;
	bool requested_shutdown;

	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	void printNearestNeighbors(const std::vector<silk::embedding::Embedding> &embeddings, const bool jsonOutput)
	{
		if (jsonOutput) {
			nlohmann::json silkContext;
			for (const auto &[record, distance] : embeddings) {
				silkContext.push_back({
					{ "id", record.id },
					{ "chunk", record.chunk },
					{ "source", record.source },
					{ "distance", distance }
				});
			}
			std::cout << silkContext.dump(4) << std::endl;
		} else {
			// Print the top 10 nearest neighbors
			std::cout << "Top 10 nearest neighbors:" << std::endl;
			for (size_t i = 0; i < std::min<size_t>(10, embeddings.size()); ++i) {
				const auto &[record, distance] = embeddings[i];
				std::cout << "Nearest neighbor " << i << ": Index=" << record.id << ", Angular Distance=" << distance << std::endl;
				std::cout << "   Chunk: " << record.chunk << std::endl;
				std::cout << "   Source: " << record.source << std::endl;
				std::cout << std::endl;
			}
		}
	}

	void Start(const Params &params)
	{
		orm::ItemActionsFactory actions;
		clients::DownloaderResult res = clients::DownloadModel(params.embeddingModel, actions, true, false);

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

		std::map<std::string, std::string> metadata;
		metadata = embeddingAI.ai->getMetadata();
		int contextSize = 0;
		std::string bosToken;
		std::string eosToken;
		std::string modelName;
		if (metadata.empty())
			throw std::runtime_error("Failed to retrieve model metadata");
		if (metadata.contains("context_length")) {
			contextSize = std::stoi(metadata.at("context_length"));
			if (!params.jsonOutput)
				std::cout << "Embedding Context size: " << contextSize << std::endl;
		} else {
			throw std::runtime_error("Failed to retrieve model contextSize");
		}
		if (metadata.contains("tokenizer.ggml.bos_token_id")) {
			bosToken = metadata.at("tokenizer.ggml.bos_token_id");
			if (!params.jsonOutput)
				std::cout << "BOS token: " << bosToken << std::endl;
		} else {
			if (!params.jsonOutput)
				std::cout << "BOS token not found. Using empty string." << std::endl;
		}
		if (metadata.contains("tokenizer.ggml.eos_token_id")) {
			eosToken = metadata.at("tokenizer.ggml.eos_token_id");

			if (!params.jsonOutput)
				std::cout << "EOS token: " << eosToken << std::endl;
		} else {
			if (!params.jsonOutput)
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
		if (!params.jsonOutput)
			std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();

		silk::embedding::EmbeddingIndex embeddingIndex(params.memoryBankName, static_cast<int>(embeddingDimensions));
		embeddingIndex.load();
		std::vector<silk::control::Message> messages;
		messages.emplace_back(silk::control::Message{ "system", "You are a friendly assistant." });
		disableInferenceLogging = true;
		while (true) {
			std::string query;
			if (params.query.empty()) {	// no query provided, ask user for input
				if (!params.jsonOutput) {
					printf("\n===========================================\n");
				}
				printf("Enter query (empty to quit): ");
				std::getline(std::cin, query);
				if (query.empty()) {
					break;
				}
			} else {
				query = params.query;
			}
			auto rtrResp = embeddingAI.sendRetrieverRequest(bosToken + query + eosToken);
			if (!rtrResp) {
				throw std::runtime_error("Failed to retrieve response");
			}

			const auto embeddings = embeddingIndex.getEmbeddings(rtrResp.value(), 10);

			if (!embeddings) {
				throw std::runtime_error("Failed to retrieve embeddings");
			}
			printNearestNeighbors(embeddings.value(), params.jsonOutput);
			nlohmann::json silkContext = embeddingIndex.getSilkContext(embeddings.value());
			if (!params.jsonOutput) {
				printf("\n===========================================\n");
			}
			if (!params.query.empty()) {	// query provided, exit after processing it
				break;
			}
		}

		embeddingAI.stop();
	}

	static void ParseParams(int argc, char **argv, Params &params)
	{
		std::string arg;
		bool invalidParam = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--memory-bank") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.memoryBankName = argv[i];
			} else if (arg == "--port") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingPort = std::stoi(argv[i]);
			} else if (arg == "--query") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.query = argv[i];
			} else if (arg == "--json-output") {
				params.jsonOutput = true;
			} else if (arg == "--embedding-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingModel = argv[i];
			} else if (arg == "--help" || arg == "-?" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  --memory-bank <name>        Input file base name. Default: embeddings" << std::endl;
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
	// disable spdlog logging
	spdlog::set_level(spdlog::level::off);
	auto params = wingman::apps::Params();

	wingman::apps::ParseParams(argc, argv, params);
	wingman::apps::Start(params);
}
