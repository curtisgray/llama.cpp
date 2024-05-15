#include "llama.h"
#include "metadata.h"
#include <vector>
#include <optional>

#include "json.hpp"
#include "llama.hpp"
#include "orm.h"
#include "spdlog/spdlog.h"

namespace wingman {

	// Set the default chat template to chatml and save to DefaultChatTemplate global static variable
	ChatTemplate default_chat_template = ChatTemplate("chatml", "ChatML", "Supports chatml format.");

	std::vector<ChatTemplate> GetSupportedChatTemplates()
	{
		std::vector<ChatTemplate> templates;

		// Add templates to the list with alias, proper name, and description
		// templates.emplace_back("chatml", "ChatML", "Supports chatml format.");
		templates.emplace_back(default_chat_template);
		templates.emplace_back("llama2", "Llama 2", "Supports llama2 format including various subformats.");
		templates.emplace_back("zephyr", "Zephyr", "Supports zephyr format.");
		templates.emplace_back("monarch", "Monarch", "Supports monarch format as used in mlabonne/AlphaMonarch-7B.");
		templates.emplace_back("gemma", "Gemma", "Supports google/gemma-7b-it format.");
		templates.emplace_back("orion", "Orion", "Supports OrionStarAI/Orion-14B-Chat format.");
		templates.emplace_back("openchat", "OpenChat", "Supports openchat/openchat-3.5-0106 format.");
		templates.emplace_back("vicuna", "Vicuna", "Supports eachadea/vicuna-13b-1.1 format.");
		templates.emplace_back("deepseek", "DeepSeek", "Supports deepseek-ai/deepseek-coder-33b-instruct format.");
		templates.emplace_back("command-r", "Command-R", "Supports CohereForAI/c4ai-command-r-plus format.");
		templates.emplace_back("llama3", "Llama 3", "Supports Llama 3 format.");
		templates.emplace_back("phi3", "Phi 3", "Supports Phi 3 format.");

		return templates;
	}

	// Function taken from llama_chat_apply_template_internal
	// This function uses heuristic checks to determine commonly used template. It is not a jinja parser.
	ChatTemplate ParseChatTemplate(const std::string &tmpl)
	{
		// Taken from the research: https://github.com/ggerganov/llama.cpp/issues/5527
		std::string templateName = "chatml";
		if (tmpl.find("<|im_start|>") != std::string::npos) {
			templateName = "chatml";
		} else if (tmpl.find("[INST]") != std::string::npos) {
			templateName = "llama2";
		} else if (tmpl.find("<|user|>") != std::string::npos) {
			templateName = "zephyr";
		} else if (tmpl.find("bos_token + message['role']") != std::string::npos) {
			templateName = "monarch";
		} else if (tmpl.find("<start_of_turn>") != std::string::npos) {
			templateName = "gemma";
		} else if (tmpl.find("'\\n\\nAssistant: ' + eos_token") != std::string::npos) {
			templateName = "orion";
		} else if (tmpl.find("GPT4 Correct ") != std::string::npos) {
			templateName = "openchat";
		} else if (tmpl.find("USER: ") != std::string::npos && tmpl.find("ASSISTANT: ") != std::string::npos) {
			templateName = "vicuna";
		} else if (tmpl.find("### Instruction:") != std::string::npos && tmpl.find("<|EOT|>") != std::string::npos) {
			templateName = "deepseek";
		} else if (tmpl.find("<|START_OF_TURN_TOKEN|>") != std::string::npos && tmpl.find("<|USER_TOKEN|>") != std::string::npos) {
			templateName = "command-r";
		} else if (tmpl.find("<|start_header_id|>") != std::string::npos && tmpl.find("<|end_header_id|>") != std::string::npos) {
			templateName = "llama3";
		} else if (tmpl.find("<|assistant|>") != std::string::npos && tmpl.find("<|end|>") != std::string::npos) {
			templateName = "phi3";
		}
		const auto supportedTemplates = GetSupportedChatTemplates();
		const auto it = std::find_if(
			supportedTemplates.begin(), supportedTemplates.end(),
			[&](const ChatTemplate &t) { return t.alias == templateName; });
		if (it != supportedTemplates.end()) {
			return *it;
		}
		return default_chat_template;
	}

	// std::optional<nlohmann::json> ExtractModelMetadata(const std::string &modelFilePath)
	// {
	// 	// Load the model (vocab_only as we only need metadata)
	// 	llama_model_params params = llama_model_default_params();
	// 	params.vocab_only = true;
	// 	llama_model *model = llama_load_model_from_file(modelFilePath.c_str(), params);
	// 	if (!model) {
	// 		spdlog::error("Failed to load model from {}", modelFilePath);
	// 		return std::nullopt;
	// 	}
	//
	// 	// extract model->gguf_kv into json
	// 	const auto metaCount = llama_model_meta_count(model);
	// 	std::map<std::string, std::string> metadata;
	// 	for (int i = 0; i < metaCount; i++) {
	// 		std::vector<char> key(1024, 0);
	// 		const auto keySize = llama_model_meta_key_by_index(model, i, key.data(), key.size());
	// 		key.resize(keySize);
	// 		std::vector<char> value(4096, 0);
	// 		const auto valueSize = llama_model_meta_val_str(model, key.data(), value.data(), value.size());
	// 		value.resize(valueSize);
	// 		const std::string keyStr(key.data(), key.size());
	// 		const std::string valueStr(value.data(), value.size());
	//
	// 		metadata[keyStr] = valueStr;
	// 	}
	//
	// 	// Free the model as we no longer need it
	// 	llama_free_model(model);
	//
	// 	if (metadata.empty()) {
	// 		// Template not found in metadata
	// 		return std::nullopt;
	// 	}
	//
	// 	nlohmann::json j = metadata;
	// 	return j;
	// }

	std::optional<nlohmann::json> ExtractModelMetadata(const std::string &modelFilePath)
	{
		const auto metadata = ModelLoader::loadMetadata(modelFilePath);
		if (!metadata) {
			// Template not found in metadata
			return std::nullopt;
		}
	
		nlohmann::json j = metadata.value();
		return j;
	}

	std::optional<nlohmann::json> GetModelMetadata(const std::string &modelRepo, const std::string &filePath, orm::ItemActionsFactory &actionsFactory)
	{
		const auto key = actionsFactory.download()->getDownloadItemFileName(modelRepo, filePath);
		auto metadata = actionsFactory.app()->getValue(key);
		if (!metadata) {
			const auto getOutputFilePath = actionsFactory.download()->getDownloadItemOutputPath(modelRepo, filePath);
			metadata = ExtractModelMetadata(getOutputFilePath);
			if (metadata)
				actionsFactory.app()->setValue(key, metadata.value());
		}
		return metadata;
	}

	std::optional<ChatTemplate> GetChatTemplate(const std::string &modelRepo, const std::string &filePath, orm::ItemActionsFactory &actionsFactory)
	{
		const auto metadata = GetModelMetadata(modelRepo, filePath, actionsFactory);
		if (metadata && metadata.value().contains("tokenizer.chat_template")) {
			const auto chatTemplate = metadata.value()["tokenizer.chat_template"].get<std::string>();
			return ParseChatTemplate(chatTemplate);
		}
		return default_chat_template;
	}

	std::optional<nlohmann::json> GetModelInfo(const std::string &modelRepo, const std::string &filePath, orm::ItemActionsFactory &actionsFactory)
	{
		const auto metadata = GetModelMetadata(modelRepo, filePath, actionsFactory);
		if (!metadata) {
			spdlog::error(" (GetModelInfo) Model metadata not found: {}:{}", modelRepo, filePath);
			return std::nullopt;
		}
		nlohmann::json info;
		info["modelRepo"] = modelRepo;
		info["filePath"] = filePath;
		// ensure chat_template is present
		if (!metadata.value().contains("tokenizer.chat_template")) {
			spdlog::error(" (GetModelInfo) Model metadata does not contain chat_template: {}:{}", modelRepo, filePath);
			return std::nullopt;
		}
		nlohmann::json j = ParseChatTemplate(metadata.value()["tokenizer.chat_template"]);
		info["chatTemplateInfo"] = j;
		info["metadata"] = metadata.value();
		return info;
	}
} // namespace wingman
