// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <annoy/annoylib.h>

#include "llama.hpp"
#include "owned_cstrings.h"
#include "curl.h"
#include "exceptions.h"
#include "ingest.h"
#include "types.h"

namespace wingman::tools {
	struct Params {
		// std::string inputPath;
		bool loadAI = false;
		std::string baseInputFilename = "embeddings";
		std::string query = "What is the influence of a base model verses a training model on LoRA?";
	};

	orm::ItemActionsFactory actions_factory;
	WingmanItemStatus inferenceStatus;

	bool OnInferenceProgressDefault(const nlohmann::json &metrics)
	{
		return true;
	}

	void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
	{
		inferenceStatus = status;
	}

	void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{}

	std::shared_ptr<ModelLoader> InitializeAI()
	{
		const auto &model = "TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf";
		return std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
	}

	const ModelGenerator::token_callback onNewToken = [](const std::string &token) {
		std::cout << token;
	};

	std::optional<nlohmann::json> sendRetreiverRequest(const std::string &query, const int port = 45678)
	{
		nlohmann::json response;
		std::string response_body;
		bool success = false;
		// Initialize curl globally
		curl_global_init(CURL_GLOBAL_DEFAULT);

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Set the URL for the POST request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(port) + "/embedding").c_str());

			// Specify the POST data
			// first wrap the query in a json object
			const nlohmann::json j = {
				{ "input", query }
			};
			const std::string json = j.dump();
			const size_t content_length = json.size();
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(content_length)).c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			const auto writeFunction = +[](void *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				const auto body = static_cast<std::string *>(userdata);
				const auto bytes = static_cast<std::byte *>(contents);
				const auto numBytes = size * nmemb;
				body->append(reinterpret_cast<const char *>(bytes), numBytes);
				return size * nmemb;
			};
			// Set write callback function to append data to response_body
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
			} else {
				// Parse the response body as JSON
				response = nlohmann::json::parse(response_body);
				success = true;
			}
		}

		// Cleanup curl globally
		curl_global_cleanup();

		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	std::tuple<std::function<void()>, std::thread> StartAI(const ModelLoader &ai, const int port)
	{
		std::cout << "Generating with model: " << ai.modelName() << std::endl;

		const auto filename = std::filesystem::path(ai.getModelPath()).filename().string();
		const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
		if (!dli) {
			std::cerr << "Failed to parse download item name from safe file path" << std::endl;
			return {};
		}
		std::map<std::string, std::string> options;
		options["--port"] = std::to_string(port);
		options["--model"] = ai.getModelPath();
		options["--alias"] = dli.value().filePath;
		options["--gpu-layers"] = "99";
		options["--embedding"] = "";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		args.emplace_back("generate");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			if (!value.empty()) {
				args.push_back(value);
			}
		}
		owned_cstrings cargs(args);
		std::function<void()> requestShutdownInference;
		inferenceStatus = WingmanItemStatus::unknown;
		std::thread inferenceThread([&ai, &cargs, &requestShutdownInference]() {
			ai.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		});

		while (inferenceStatus != WingmanItemStatus::inferring) {
			fmt::print("{}: {}\t\t\t\r", ai.modelName(), WingmanItem::toString(inferenceStatus));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << std::endl;
		return { std::move(requestShutdownInference), std::move(inferenceThread) };
	};

	void Start(Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".db").filename().string()).string();

		std::function<void()> aiShutdown;
		std::thread aiThread;
		int port = 45679;
		if (params.loadAI) {
			auto ai = InitializeAI();
			std::tie(aiShutdown, aiThread) = StartAI(*ai, port);
		} else {
			port = 6567;
		}

		silk::ingestion::AnnoyIndex annoyIndex(384);
		annoyIndex.load(annoyFilePath.c_str());
		silk::ingestion::EmbeddingDb db(dbPath);

		while (true) {
			printf("Enter query (empty to quit): ");
			std::getline(std::cin, params.query);
			if (params.query.empty()) {
				break;
			}
			auto rtrResp = sendRetreiverRequest(util::stringTrim(params.query), port);
			if (!rtrResp) {
				throw std::runtime_error("Failed to retrieve response");
			}
			std::vector<float> queryEmbedding = silk::ingestion::ExtractEmbeddingFromJson(rtrResp.value());

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
			aiShutdown();
			aiThread.join();
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
