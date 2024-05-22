// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include "llama.hpp"
#include "owned_cstrings.h"
#include "curl.h"
#include "exceptions.h"
#include "ingest.h"
#include "types.h"
#include "progressbar.hpp"

namespace wingman::tools {
	struct Params {
		std::string inputPath;
		size_t chunkSize = 0;
		bool loadAI = false;
		std::string baseOutputFilename = "embeddings";
		size_t maxEmbeddingSize = 4096;
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
				std::cerr << std::endl << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
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
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseOutputFilename + ".db").filename().string()).string();

		// delete both files if they exist
		std::filesystem::remove(annoyFilePath);
		std::filesystem::remove(dbPath);

		std::function<void()> aiShutdown;
		std::thread aiThread;
		int port = 45679;
		if (params.loadAI) {
			auto ai = InitializeAI();
			std::tie(aiShutdown, aiThread) = StartAI(*ai, port);
		} else {
			port = 6567;
		}
		auto r = sendRetreiverRequest("Hello world. This is a test.", port);
		if (!r) {
			throw std::runtime_error("Getting dimensions: Failed to retrieve response");
		}
		std::vector<float> s= silk::ingestion::ExtractEmbeddingFromJson(r.value());
		if (s.empty()) {
			throw std::runtime_error("Getting dimensions: Failed to extract embedding from response");
		}
		std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();
		if (params.chunkSize == 0)
			params.chunkSize = embeddingDimensions;

		silk::ingestion::EmbeddingDb db(dbPath);

		wingman::silk::ingestion::AnnoyIndex annoyIndex(embeddingDimensions);
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
		// 	R"(C:\Users\curtis.CARVERLAB\source\repos\tt\docs\Expert Prompting Method - Exploring the MIT Mathematics and EECS Curriculum Using Large Language Models.pdf)");

		// Process the directory or file...
		auto index = 0;
		for (const auto &pdfFilePath : pdfFiles) {
			auto start_time = std::chrono::high_resolution_clock::now();
			const auto pdfFile = pdfFilePath.string();
			std::cout << "PDF [" << ++index << " of " << pdfFiles.size() << "] " << pdfFile << std::endl;
			std::cout << "  Chunking..." << std::endl;
			const auto chunked_data = silk::ingestion::ChunkPdfText(pdfFile, params.chunkSize, params.maxEmbeddingSize);

			if (!chunked_data) {
				throw std::runtime_error("Failed to chunk PDF text: " + pdfFile);
			}

			// Process the chunked data...
			for (const auto &[pdfName, chunkTypes] : chunked_data.value()) {
				std::cout << "  Ingesting..." << std::endl;
				for (const auto &[chunk_type, chunks] : chunkTypes) {
					std::cout << "    " << chunk_type << ":" << std::endl;
					bar.reset();
					bar.set_niter(chunks.size() * 3);
					for (const auto &chunk : chunks) {
						std::string c(chunk);
						// std::cout << "+";
						bar.update();
						std::optional<nlohmann::json> rtrResp;
						int maxRetries = 3;
						do {
							rtrResp = sendRetreiverRequest(util::stringTrim(c), port);
							if (!rtrResp) {
								std::cerr << "Failed to retrieve response. Retrying..." << std::endl;
							} else {
								break;
							}
							// pause for a bit
							std::this_thread::sleep_for(std::chrono::milliseconds(300));
						} while (!rtrResp && --maxRetries > 0);
						if (!rtrResp) {
							throw std::runtime_error("Failed to retrieve response");
						}
						std::vector<float> storageEmbedding = silk::ingestion::ExtractEmbeddingFromJson(rtrResp.value());
						if (storageEmbedding.size() != embeddingDimensions) {
							// throw std::runtime_error("Invalid embedding size: " + std::to_string(storageEmbedding.size()) + ". Expected: " + std::to_string(embeddingDimensions));
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

		annoyIndex.build(embeddingDimensions * embeddingDimensions);

		auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_embedding_time).count();
		std::cout << "Total embedding time: " << total_seconds / 60 << ":"
			<< total_seconds % 60 << std::endl;

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
