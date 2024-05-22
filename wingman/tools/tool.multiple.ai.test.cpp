// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define dev_null "nul"
#else
#include <unistd.h>
#include <fcntl.h>
#define dev_null "/dev/null"
#endif
#include <spdlog/spdlog.h>
#include "llama.hpp"
#include "owned_cstrings.h"
#include "curl.h"
#include "types.h"

namespace wingman::tools {
	enum Role {
		system,
		assistant,
		user
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(Role, {
		{ Role::system, "system" },
		{ Role::assistant, "assistant" },
		{ Role::user, "user" }
	})

	struct Message {
		std::string role;
		std::string content;
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, role, content)

	orm::ItemActionsFactory actions_factory;
	WingmanItemStatus inferenceStatus;

	bool OnInferenceProgressDefault(const nlohmann::json &metrics)
	{
		return true;
	}

	bool OnInferenceProgress(const nlohmann::json &metrics)
	{
		return true;
	}

	void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
	{
		inferenceStatus = status;
	}

	void OnInferenceStatus(const std::string &alias, const WingmanItemStatus &status)
	{
		inferenceStatus = status;
		auto wi = actions_factory.wingman()->get(alias);
		if (wi) {
			wi.value().status = status;
			actions_factory.wingman()->set(wi.value());
		} else {
			spdlog::error(" ***(OnInferenceStatus) Alias {} not found***", alias);
		}
	}

	void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{}

	void OnInferenceServiceStatus(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{
		auto appItem = actions_factory.app()->get("WingmanService").value_or(AppItem::make("WingmanService"));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto wingmanServerItem = j.get<WingmanServiceAppItem>();
		wingmanServerItem.status = status;
		if (error) {
			wingmanServerItem.error = error;
		}
		nlohmann::json j2 = wingmanServerItem;
		appItem.value = j2.dump();
		actions_factory.app()->set(appItem);
	}

	std::tuple<std::shared_ptr<ModelLoader>, std::shared_ptr<ModelLoader>> InitializeLoaders()
	{
		const std::vector<std::string> models = {
			// "jinaai[-]jina-embeddings-v2-base-en[=]jina-embeddings-v2-base-en-f16.gguf",
			// "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf",
			// "TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
			"second-state[-]All-MiniLM-L6-v2-Embedding-GGUF[=]all-MiniLM-L6-v2-Q5_K_M.gguf",
			"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
		};
		std::vector<std::shared_ptr<ModelLoader>> loaders;
		std::map<std::string, std::string> options;

		bool first = true;
		for (const auto &model : models) {
			if (first) {
				auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
				loaders.push_back(loader);
				first = false;
			} else {
				// auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgress, OnInferenceStatus, OnInferenceServiceStatus);
				auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
				loaders.push_back(loader);
			}
		}

		return { loaders[0], loaders[1] };
	}

	std::tuple<std::shared_ptr<ModelGenerator>, std::shared_ptr<ModelGenerator>> InitializeGenerators()
	{
		const std::vector<std::string> models = {
			// "jinaai[-]jina-embeddings-v2-base-en[=]jina-embeddings-v2-base-en-f16.gguf",
			// "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf",
			// "TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
			"second-state[-]All-MiniLM-L6-v2-Embedding-GGUF[=]all-MiniLM-L6-v2-Q5_K_M.gguf",
			"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
		};
		std::vector<std::shared_ptr<ModelGenerator>> generators;
		std::map<std::string, std::string> options;

		bool first = true;
		for (const auto &model : models) {
			options["--model"] = model;
			options["--alias"] = model;
			if (first) {
				options["--gpu-layers"] = "0";
				first = false;
			} else {
				options["--gpu-layers"] = "99";
			}

			// join pairs into a char** argv compatible array
			std::vector<std::string> args;
			args.emplace_back("wingman");
			for (const auto &[option, value] : options) {
				args.push_back(option);
				if (!value.empty()) {
					args.push_back(value);
				}
			}
			owned_cstrings cargs(args);
			auto generator = std::make_shared<ModelGenerator>(static_cast<int>(cargs.size() - 1), cargs.data());
			generators.push_back(generator);
		}

		return { generators[0], generators[1] };
	}

	const ModelGenerator::token_callback onNewToken = [](const std::string &token) {
		std::cout << token;
	};

	void Generate(const ModelGenerator &generator, const std::string &prompt, const bool isRetrieval)
	{
		gpt_params params;
		int maxTokensToGenerate;
		if (isRetrieval) {
			maxTokensToGenerate = 512;
			// For BERT models, batch size must be equal to ubatch size
			// ReSharper disable once CppLocalVariableMightNotBeInitialized
			params.n_ubatch = params.n_batch;
			params.embedding = true;
		} else {
			maxTokensToGenerate = 1024;
		}
		params.prompt = prompt;

		constexpr std::atomic_bool tokenGenerationCancelled = false;
		std::cout << "Generating tokens for model: " << generator.modelName() << std::endl;
		generator.generate(params, maxTokensToGenerate, onNewToken, tokenGenerationCancelled);
		std::cout << std::endl;
	};

	void Generate(const ModelGenerator &generator, const char *promptStr, const bool isRetrieval)
	{
		const std::string prompt = promptStr;
		Generate(generator, prompt, isRetrieval);
	}

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

	// Function to get the API key from the environment
	std::string GetOpenAIApiKey()
	{
		char *api_key = std::getenv("OPENAI_API_KEY");
		if (api_key == nullptr) {
			std::cerr << "Error: OPENAI_API_KEY environment variable not set." << std::endl;
			return "";
		}
		return std::string(api_key);
	}

	// Function to call the OpenAI API completion endpoint with a prompt and handle SSE
	std::optional<nlohmann::json> sendChatCompletionRequest(const std::vector<Message> messages, const std::string &modelName, const int port = 45679)
	{
		nlohmann::json response;
		std::string response_body;
		bool success = false;
		const std::string url = "http://localhost:" + std::to_string(port) + "/v1/chat/completions";

		curl_global_init(CURL_GLOBAL_ALL);
		if (const auto curl = curl_easy_init()) {
			const auto headerCallback = +[](const char *contents, const size_t size, const size_t nmemb, void *userp) -> size_t {
				const auto numBytes = size * nmemb;
				return numBytes;
			};
			const auto eventCallback = +[](const char *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				// Extract the event data and display it
				const std::string event_data(contents, size * nmemb);
				// std::cout << event_data << std::endl;
				const auto j = nlohmann::json::parse(event_data);
				auto completion = j["choices"][0]["message"]["content"].get<std::string>();
				// look for `<|im_end|>` in the completion and remove it
				const auto end_pos = completion.find("<|im_end|>");
				if (end_pos != std::string::npos) {
					completion.erase(end_pos);
				}
				std::cout << completion << std::endl;

				const auto body = static_cast<std::string *>(userdata);
				const auto numBytes = size * nmemb;
				body->append(contents, numBytes);

				return numBytes;
			};

			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

			// Create the JSON object to send to the API
			nlohmann::json j;
			j["messages"] = messages;
			j["model"] = modelName;
			j["max_tokens"] = 100;
			j["temperature"] = 0.7;

			const std::string json = j.dump();
			const size_t content_length = json.size();
			struct curl_slist *headers = nullptr;
			// headers = curl_slist_append(headers, ("Authorization: Bearer " + API_KEY).c_str());
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(content_length)).c_str());

			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, eventCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			// Set up curl for receiving SSE events
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);

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
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
		}

		curl_global_cleanup();
		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	std::tuple<std::function<void()>, std::thread> StartRetreiver(ModelLoader &retriever, const int port)
	{
		std::cout << "Retrieving with model: " << retriever.modelName() << std::endl;

		const auto filename = std::filesystem::path(retriever.getModelPath()).filename().string();
		const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
		if (!dli) {
			std::cerr << "Failed to parse download item name from safe file path" << std::endl;
			return {};
		}
		std::map<std::string, std::string> options;
		options["--port"] = std::to_string(port);
		options["--model"] = retriever.getModelPath();
		options["--alias"] = dli.value().filePath;
		options["--gpu-layers"] = "4";
		options["--embedding"] = "";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		args.emplace_back("retrieve");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			if (!value.empty()) {
				args.push_back(value);
			}
		}
		owned_cstrings cargs(args);
		std::function<void()> requestShutdownInference;
		inferenceStatus = WingmanItemStatus::unknown;
		std::thread inferenceThread([&retriever, &cargs, &requestShutdownInference]() {
			retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		});

		while (inferenceStatus != WingmanItemStatus::inferring) {
			fmt::print("{}: {}\t\t\t\r", retriever.modelName(), WingmanItem::toString(inferenceStatus));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << std::endl;
		// retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		// retriever.retrieve(query);
		return { std::move(requestShutdownInference), std::move(inferenceThread) };
	}

	std::tuple<std::function<void()>, std::thread> StartGenerator(const ModelLoader &generator, const int port)
	{
		std::cout << "Generating with model: " << generator.modelName() << std::endl;

		const auto filename = std::filesystem::path(generator.getModelPath()).filename().string();
		const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
		if (!dli) {
			std::cerr << "Failed to parse download item name from safe file path" << std::endl;
			return {};
		}
		std::map<std::string, std::string> options;
		options["--port"] = std::to_string(port);
		options["--model"] = generator.getModelPath();
		options["--alias"] = dli.value().filePath;
		options["--gpu-layers"] = "99";

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
		std::thread inferenceThread([&generator, &cargs, &requestShutdownInference]() {
			generator.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		});

		while (inferenceStatus != WingmanItemStatus::inferring) {
			fmt::print("{}: {}\t\t\t\r", generator.modelName(), WingmanItem::toString(inferenceStatus));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << std::endl;
		return { std::move(requestShutdownInference), std::move(inferenceThread) };
	};

	void Start()
	{
		// auto [retriever, generator] = InitializeGenerators();
		//
		// std::cout << "Retriever model: " << retriever->modelName() << std::endl;
		// Generate(*retriever, "Hello, my name is", true);
		//
		// std::cout << "Generator model: " << generator->modelName() << std::endl;
		// Generate(*generator, "Hello, my name is", false);
		auto [retriever, generator] = InitializeLoaders();
		std::cout << "Retriever model: " << retriever->modelName() << std::endl;
		auto [retrieverShutdown, retrieverThread] = StartRetreiver(*retriever, 45678);

		// auto query = "At vero eos et accusamus et iusto odio dignissimos "
		// 			"ducimus qui blanditiis praesentium voluptatum deleniti "
		// 			"atque corrupti quos dolores et quas molestias excepturi "
		// 			"sint occaecati cupiditate non provident, similique sunt "
		// 			"in culpa qui officia deserunt mollitia animi, id est "
		// 			"laborum et dolorum fuga.";
		auto retreivalQuery = "At vero eos et accusamus et iusto odio dignissimos "
					 "laborum et dolorum fuga.";
		auto rtrResp = sendRetreiverRequest(retreivalQuery);
		if (rtrResp) {
			std::cout << "Response: " << rtrResp.value() << std::endl;
		} else {
			std::cerr << "Failed to retrieve response" << std::endl;
		}

		std::cout << "Generator model: " << generator->modelName() << std::endl;
		auto [generatorShutdown, generatorThread] = StartGenerator(*generator, 45679);

		std::vector<Message> messages = {
			{ "system", "You are a helpful assistant." },
			{ "user", "Hello there, how are you?" },
		};
		auto genResp = sendChatCompletionRequest(messages, generator->modelName());
		if (genResp) {
			std::cout << "Response: " << genResp.value() << std::endl;
		} else {
			std::cerr << "Failed to retrieve response" << std::endl;
		}
		// add response to messages
		messages.push_back({ "assistant", genResp.value()["choices"][0]["message"]["content"] });

		rtrResp = sendRetreiverRequest(retreivalQuery);
		if (rtrResp) {
			std::cout << "Response: " << rtrResp.value() << std::endl;
		} else {
			std::cerr << "Failed to retrieve response" << std::endl;
		}

		genResp = sendChatCompletionRequest(messages, generator->modelName());
		messages.push_back({ "user", ("Can you decode the following tokens? " + rtrResp.value()["embedding"].get<std::string>()) });
		if (genResp) {
			std::cout << "Response: " << genResp.value() << std::endl;
		} else {
			std::cerr << "Failed to retrieve response" << std::endl;
		}
		generatorShutdown();
		generatorThread.join();
		retrieverShutdown();
		retrieverThread.join();
	}
}

int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);

	wingman::tools::Start();
}
