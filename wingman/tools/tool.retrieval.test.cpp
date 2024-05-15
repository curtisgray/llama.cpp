// ReSharper disable CppInconsistentNaming
#include <spdlog/spdlog.h>
#include "llama.hpp"
#include "owned_cstrings.h"
#include "curl.h"

namespace wingman::tools {
	// orm::ItemActionsFactory actions_factory;
	//
	// bool OnInferenceProgressDefault(const nlohmann::json &metrics)
	// {
	// 	return true;
	// }
	//
	// bool OnInferenceProgress(const nlohmann::json &metrics)
	// {
	// 	return true;
	// }
	//
	// void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
	// {}
	//
	// void OnInferenceStatus(const std::string &alias, const WingmanItemStatus &status)
	// {
	// 	auto wi = actions_factory.wingman()->get(alias);
	// 	if (wi) {
	// 		wi.value().status = status;
	// 		actions_factory.wingman()->set(wi.value());
	// 	} else {
	// 		spdlog::error(" ***(OnInferenceStatus) Alias {} not found***", alias);
	// 	}
	// }
	//
	// void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)
	// {}
	//
	// void OnInferenceServiceStatus(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)
	// {
	// 	auto appItem = actions_factory.app()->get("WingmanService").value_or(AppItem::make("WingmanService"));
	//
	// 	nlohmann::json j = nlohmann::json::parse(appItem.value);
	// 	auto wingmanServerItem = j.get<WingmanServiceAppItem>();
	// 	wingmanServerItem.status = status;
	// 	if (error) {
	// 		wingmanServerItem.error = error;
	// 	}
	// 	nlohmann::json j2 = wingmanServerItem;
	// 	appItem.value = j2.dump();
	// 	actions_factory.app()->set(appItem);
	// }
	//
	// std::function<void()> shutdown_inference;
	//
	// std::tuple<std::shared_ptr<ModelLoader>, std::shared_ptr<ModelLoader>> InitializeLoaders()
	// {
	// 	const std::vector<std::string> models = {
	// 		// "jinaai[-]jina-embeddings-v2-base-en[=]jina-embeddings-v2-base-en-f16.gguf",
	// 		// "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf",
	// 		// "TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
	// 		"second-state[-]All-MiniLM-L6-v2-Embedding-GGUF[=]all-MiniLM-L6-v2-Q5_K_M.gguf",
	// 		"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
	// 	};
	// 	std::vector<std::shared_ptr<ModelLoader>> loaders;
	// 	std::map<std::string, std::string> options;
	//
	// 	bool first = true;
	// 	for (const auto &model : models) {
	// 		if (first) {
	// 			auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
	// 			loaders.push_back(loader);
	// 			first = false;
	// 		} else {
	// 			auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgress, OnInferenceStatus, OnInferenceServiceStatus);
	// 			loaders.push_back(loader);
	// 		}
	// 	}
	//
	// 	return { loaders[0], loaders[1] };
	// }
	//
	// std::tuple<std::shared_ptr<ModelGenerator>, std::shared_ptr<ModelGenerator>> InitializeGenerators()
	// {
	// 	const std::vector<std::string> models = {
	// 		// "jinaai[-]jina-embeddings-v2-base-en[=]jina-embeddings-v2-base-en-f16.gguf",
	// 		// "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf",
	// 		// "TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
	// 		"second-state[-]All-MiniLM-L6-v2-Embedding-GGUF[=]all-MiniLM-L6-v2-Q5_K_M.gguf",
	// 		"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
	// 	};
	// 	std::vector<std::shared_ptr<ModelGenerator>> generators;
	// 	std::map<std::string, std::string> options;
	//
	// 	bool first = true;
	// 	for (const auto &model : models) {
	// 		options["--model"] = model;
	// 		options["--alias"] = model;
	// 		if (first) {
	// 			options["--gpu-layers"] = "0";
	// 			first = false;
	// 		} else {
	// 			options["--gpu-layers"] = "99";
	// 		}
	//
	// 		// join pairs into a char** argv compatible array
	// 		std::vector<std::string> args;
	// 		args.emplace_back("wingman");
	// 		for (const auto &[option, value] : options) {
	// 			args.push_back(option);
	// 			args.push_back(value);
	// 		}
	// 		owned_cstrings cargs(args);
	// 		auto generator = std::make_shared<ModelGenerator>(static_cast<int>(cargs.size() - 1), cargs.data());
	// 		generators.push_back(generator);
	// 	}
	//
	// 	return { generators[0], generators[1] };
	// }
	//
	// const ModelGenerator::token_callback onNewToken = [](const std::string &token) {
	// 	std::cout << token;
	// };
	//
	// void Generate(const ModelGenerator &generator, const std::string &prompt, const bool isRetrieval)
	// {
	// 	gpt_params params;
	// 	int maxTokensToGenerate;
	// 	if (isRetrieval) {
	// 		maxTokensToGenerate = 512;
	// 		// For BERT models, batch size must be equal to ubatch size
	// 		// ReSharper disable once CppLocalVariableMightNotBeInitialized
	// 		params.n_ubatch = params.n_batch;
	// 		params.embedding = true;
	// 	} else {
	// 		maxTokensToGenerate = 1024;
	// 	}
	// 	params.prompt = prompt;
	//
	// 	constexpr std::atomic_bool tokenGenerationCancelled = false;
	// 	std::cout << "Generating tokens for model: " << generator.modelName() << std::endl;
	// 	generator.generate(params, maxTokensToGenerate, onNewToken, tokenGenerationCancelled);
	// 	std::cout << std::endl;
	// };
	//
	// void Generate(const ModelGenerator &generator, const char *promptStr, const bool isRetrieval)
	// {
	// 	const std::string prompt = promptStr;
	// 	Generate(generator, prompt, isRetrieval);
	// }
	//
	// void Retrieve(ModelLoader &retriever, const std::string &query)
	// {
	// 	std::cout << "Retrieving for model: " << retriever.modelName() << std::endl;
	// 	std::function<void()> &requestShutdownInference = shutdown_inference;
	// 	std::map<std::string, std::string> options;
	// 	options["--port"] = "45678";
	// 	options["--model"] = retriever.getModelPath();
	// 	options["--alias"] = retriever.getModelPath();
	// 	options["--gpu-layers"] = "4";
	// 	options["--prompt"] = query;
	// 	options["--embedding"] = "true";
	//
	// 	// join pairs into a char** argv compatible array
	// 	std::vector<std::string> args;
	// 	args.emplace_back("wingman");
	// 	for (const auto &[option, value] : options) {
	// 		args.push_back(option);
	// 		args.push_back(value);
	// 	}
	// 	owned_cstrings cargs(args);
	// 	std::thread wingmanServiceThread([&retriever, &cargs, &requestShutdownInference]() {
	// 		retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
	// 	});
	//
	// 	// retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
	// 	// retriever.retrieve(query);
	// 	// Use libcurl to make a POST request to localhost:45678/embedding with the query
	// 	curl_global_init(CURL_GLOBAL_DEFAULT);
	// 	CURL *curl = curl_easy_init();
	// 	if (curl) {
	// 		// Set the URL
	// 		curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:45678/embedding");
	//
	// 		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query.c_str());
	//
	// 		// Perform the request
	// 		const CURLcode res = curl_easy_perform(curl);
	//
	// 		// Check for errors
	// 		if (res != CURLE_OK) {
	// 			std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
	// 		}
	//
	// 		// Clean up
	// 		curl_easy_cleanup(curl);
	// 	}
	// 	curl_global_cleanup();
	// 	requestShutdownInference();
	// }
	//
	// void Generate(const ModelLoader &generator, const std::string &prompt)
	// {
	// 	gpt_params params;
	// 	int maxTokensToGenerate = 512;
	// 	// For BERT models, batch size must be equal to ubatch size
	// 	// ReSharper disable once CppLocalVariableMightNotBeInitialized
	// 	params.n_ubatch = params.n_batch;
	// 	params.embedding = true;
	// 	params.prompt = prompt;
	//
	// 	constexpr std::atomic_bool tokenGenerationCancelled = false;
	// 	std::cout << "Generating tokens for model: " << generator.modelName() << std::endl;
	// 	// generator.generate(params, maxTokensToGenerate, onNewToken, tokenGenerationCancelled);
	// 	// std::cout << std::endl;
	// };
	//
	// void Start()
	// {
	// 	// auto [retriever, generator] = Initialize();
	// 	//
	// 	// std::cout << "Retriever model: " << retriever->modelName() << std::endl;
	// 	// Generate(*retriever, "Hello, my name is", true);
	// 	//
	// 	// std::cout << "Generator model: " << generator->modelName() << std::endl;
	// 	// Generate(*generator, "Hello, my name is", false);
	// 	auto [retriever, generator] = InitializeLoaders();
	// 	std::cout << "Retriever model: " << retriever->modelName() << std::endl;
	// 	Retrieve(*retriever, "At vero eos et accusamus et iusto odio dignissimos ducimus qui blanditiis praesentium voluptatum deleniti atque corrupti quos dolores et quas molestias excepturi sint occaecati cupiditate non provident, similique sunt in culpa qui officia deserunt mollitia animi, id est laborum et dolorum fuga.");
	//
	// 	std::cout << "Generator model: " << generator->modelName() << std::endl;
	// 	Generate(*generator, "Hello, my name is");
	// }
}

int main(int argc, char *argv[])
{
	// spdlog::set_level(spdlog::level::trace);
	//
	// wingman::tools::Start();
}
