#pragma once
#include <common.h>
#include <filesystem>
#include <regex>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <functional>

#include "orm.h"
#include "types.h"
#include "wingman.server.integration.h"

namespace wingman::silk {
	struct server_params {
		int32_t port = 8080;
		int32_t read_timeout = 600;
		int32_t write_timeout = 600;
		int32_t n_threads_http = -1;

		std::string hostname = "127.0.0.1";
		std::string public_path = "";
		std::string chat_template = "";
		std::string system_prompt = "";

		std::vector<std::string> api_keys;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
		std::string ssl_key_file = "";
		std::string ssl_cert_file = "";
#endif

		bool slots_endpoint = true;
		bool metrics_endpoint = false;
		std::string slot_save_path;
	};

	class ModelLoader {
		inline static std::string DEFAULT_MODEL_FILE = "CompendiumLabs/bge-base-en-v1.5-gguf/bge-base-en-v1.5-q8_0.gguf";

		static bool parseServerParams(int argc, char **argv, server_params &sparams, gpt_params &params, bool &didUserSetCtxSize);

		static bool parseGptParams(int argc, char **argv, gpt_params &params, bool &didUserSetCtxSize);

		static std::string llama_model_ftype_name(llama_ftype ftype);

		static void replace_all(std::string &s, const std::string &search, const std::string &replace);

		static std::string gguf_data_to_str(enum gguf_type type, const void *data, int i);

		static std::optional<std::map<std::string, std::string>> loadModelMetadata(const std::string &fname);

		static std::tuple<llama_model *, llama_context *> loadModel(gpt_params &params, bool didUserSetCtxSize = false);

		static void unloadModel(const std::tuple<llama_model *, llama_context *> &m);

		std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
		std::function<void(const std::string &alias, const WingmanItemStatus &status)> onInferenceStatus = nullptr;
		std::function<void(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)> onInferenceServiceStatus = nullptr;
		std::map<std::string, std::string> metadata;
		std::string modelPath;
		gpt_params params;
		server_params sparams;
		bool isInferring = false;
		bool lazyLoadModel = false;
	public:
		~ModelLoader();
		ModelLoader(const std::string &model,
		const std::function<bool(const nlohmann::json &metrics)> &onProgress,
		const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus,
		const std::function<void(const wingman::WingmanServiceAppItemStatus &status,
			std::optional<std::string> error)> &onServiceStatus);
		ModelLoader(const int argc, char **argv);

		std::tuple<llama_model *, llama_context *> model;

		static std::tuple<std::string, std::string> parseModelFromMoniker(const std::string &moniker);

		std::string modelName() const;

		llama_model *getModel() const;

		llama_context *getContext() const;

		std::map<std::string, std::string> getMetadata() const;

		static std::optional<std::map<std::string, std::string>> loadMetadata(const std::string &modelPath);

		std::string getModelPath() const;

		int run(const int argc, char **argv, std::function<void()> &requestShutdownInference) const;
	};

	class ModelGenerator {
		ModelLoader &loader;

	public:
		explicit ModelGenerator(ModelLoader &loader);

		ModelGenerator(const int argc, char **argv);

		std::optional<std::vector<llama_token>> tokenize(const std::string &text, int maxTokensToGenerate) const;

		using token_callback = std::function<void(const std::string &)>;

		void generate(const gpt_params &params, const int &maxTokensToGenerate, const token_callback &onNewToken, const std::atomic<bool> &cancelled) const;

		std::string modelName() const;

		std::map<std::string, std::string> metadata() const;
	};
} // namespace wingman::silk
