// ReSharper disable CppInconsistentNaming
#include <spdlog/spdlog.h>
#include "llama.hpp"
#include "owned_cstrings.h"

int main(int argc, char *argv[])
{
	constexpr bool oneAtATime = false;
	spdlog::set_level(spdlog::level::trace);
	const std::vector<std::string> models = {
		"TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
		// "reach-vb[-]Phi-3-mini-4k-instruct-Q8_0-GGUF[=]phi-3-mini-4k-instruct.Q8_0.gguf",
		"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
		// "bartowski[-]Meta-Llama-3-8B-Instruct-GGUF[=]Meta-Llama-3-8B-Instruct-Q5_K_S.gguf"
	};
	std::vector<wingman::ModelGenerator> generators;
	bool first = true;
	const wingman::ModelGenerator::token_callback onNewToken = [](const std::string &token) {
		std::cout << token;
	};
	auto generate = [&first, onNewToken](const wingman::ModelGenerator generator) {
		gpt_params params;
		params.prompt = "Tell me about tea.";

		const int maxTokensToGenerate = first ? 512 : 1024;
		constexpr std::atomic_bool tokenGenerationCancelled = false;
		std::cout << "Generating tokens for model: " << generator.modelName() << std::endl;
		generator.generate(params, maxTokensToGenerate, onNewToken, tokenGenerationCancelled);
		std::cout << std::endl;

		first = false;
	};
	std::map<std::string, std::string> options;
	for (const auto &model : models) {
		options["--model"] = model;
		options["--alias"] = model;
		options["--gpu-layers"] = "99";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		args.emplace_back("wingman");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			args.push_back(value);
		}
		owned_cstrings cargs(args);
		// wingman::ModelLoader loader(argc, argv);
		// const wingman::ModelGenerator generator(argc, argv);
		// const wingman::ModelGenerator generator(static_cast<int>(cargs.size() - 1), cargs.data());
		// generators.emplace_back(static_cast<int>(cargs.size() - 1), cargs.data());
		if (oneAtATime) {
			const wingman::ModelGenerator generator(static_cast<int>(cargs.size() - 1), cargs.data());
			generate(generator);
		} else {
			wingman::ModelGenerator generator(static_cast<int>(cargs.size() - 1), cargs.data());
			generators.push_back(generator);
		}

		first = false;
	}

	first = true;
	for (const auto &generator : generators) {
		if (oneAtATime)
			continue;
		generate(generator);
		first = false;
	}
	return 0;
}
