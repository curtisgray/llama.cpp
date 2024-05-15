#pragma once
#include <common.h>
#include <filesystem>
#include <regex>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <functional>

// Define the callback type

#include "orm.h"
#include "owned_cstrings.h"
#include "types.h"
#include "wingman.server.integration.h"

namespace wingman {
	class ModelLoader {
		inline static std::string DEFAULT_MODEL_FILE = "reach-vb[-]Phi-3-mini-4k-instruct-Q8_0-GGUF[=]phi-3-mini-4k-instruct.Q8_0.gguf";

		static bool parseParams(int argc, char **argv, gpt_params &params, bool &didUserSetCtxSize)
		{
			gpt_params defaultParams;

			std::string arg;
			bool invalidParam = false;
			const auto home = GetWingmanHome();

			for (int i = 1; i < argc; i++) {
				arg = argv[i];
				if (arg == "-m" || arg == "--model") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					// params.model = argv[i];
					// --model is the file name of the model, not the full path. Add the wingman home directory to the file name
					params.model = (home / "models" / std::filesystem::path(argv[i]).filename().string()).string();
				} else if (arg == "-mu" || arg == "--model-url") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.model_url = argv[i];
				} else if (arg == "-hfr" || arg == "--hf-repo") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.hf_repo = argv[i];
				} else if (arg == "-hff" || arg == "--hf-file") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.hf_file = argv[i];
				} else if (arg == "-a" || arg == "--alias") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.model_alias = argv[i];
				} else if (arg == "-c" || arg == "--ctx-size" || arg == "--ctx_size") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_ctx = std::stoi(argv[i]);
					didUserSetCtxSize = true;
				} else if (arg == "--rope-scaling") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					std::string value(argv[i]);
					/**/ if (value == "none") {
						params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_NONE;
					} else if (value == "linear") {
						params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_LINEAR;
					} else if (value == "yarn") {
						params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_YARN;
					} else {
						invalidParam = true; break;
					}
				} else if (arg == "--rope-freq-base") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.rope_freq_base = std::stof(argv[i]);
				} else if (arg == "--rope-freq-scale") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.rope_freq_scale = std::stof(argv[i]);
				} else if (arg == "--yarn-ext-factor") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.yarn_ext_factor = std::stof(argv[i]);
				} else if (arg == "--yarn-attn-factor") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.yarn_attn_factor = std::stof(argv[i]);
				} else if (arg == "--yarn-beta-fast") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.yarn_beta_fast = std::stof(argv[i]);
				} else if (arg == "--yarn-beta-slow") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.yarn_beta_slow = std::stof(argv[i]);
				} else if (arg == "--pooling") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					std::string value(argv[i]);
					/**/ if (value == "none") {
						params.pooling_type = LLAMA_POOLING_TYPE_NONE;
					} else if (value == "mean") {
						params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
					} else if (value == "cls") {
						params.pooling_type = LLAMA_POOLING_TYPE_CLS;
					} else {
						invalidParam = true; break;
					}
				} else if (arg == "--defrag-thold" || arg == "-dt") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.defrag_thold = std::stof(argv[i]);
				} else if (arg == "--threads" || arg == "-t") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_threads = std::stoi(argv[i]);
				} else if (arg == "--grp-attn-n" || arg == "-gan") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}

					params.grp_attn_n = std::stoi(argv[i]);
				} else if (arg == "--grp-attn-w" || arg == "-gaw") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}

					params.grp_attn_w = std::stoi(argv[i]);
				} else if (arg == "--threads-batch" || arg == "-tb") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_threads_batch = std::stoi(argv[i]);
				} else if (arg == "-b" || arg == "--batch-size") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_batch = std::stoi(argv[i]);
				} else if (arg == "-ub" || arg == "--ubatch-size") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_ubatch = std::stoi(argv[i]);
				} else if (arg == "--gpu-layers" || arg == "-ngl" || arg == "--n-gpu-layers") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					if (llama_supports_gpu_offload()) {
						params.n_gpu_layers = std::stoi(argv[i]);
					} else {
						spdlog::warn("Not compiled with GPU offload support, --n-gpu-layers option will be ignored.");
					}
				} else if (arg == "-nkvo" || arg == "--no-kv-offload") {
					params.no_kv_offload = true;
				} else if (arg == "--split-mode" || arg == "-sm") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					std::string argNext = argv[i];
					if (argNext == "none") {
						params.split_mode = LLAMA_SPLIT_MODE_NONE;
					} else if (argNext == "layer") {
						params.split_mode = LLAMA_SPLIT_MODE_LAYER;
					} else if (argNext == "row") {
						params.split_mode = LLAMA_SPLIT_MODE_ROW;
					} else {
						invalidParam = true;
						break;
					}
				} else if (arg == "--tensor-split" || arg == "-ts") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
#if defined(GGML_USE_CUDA) || defined(GGML_USE_SYCL)
					std::string argNext = argv[i];

					// split string by , and /
					const std::regex regex{ R"([,/]+)" };
					std::sregex_token_iterator it{ argNext.begin(), argNext.end(), regex, -1 };
					std::vector<std::string> splitArg{ it, {} };
					GGML_ASSERT(splitArg.size() <= llama_max_devices());

					for (size_t iDevice = 0; iDevice < llama_max_devices(); ++iDevice) {
						if (iDevice < splitArg.size()) {
							params.tensor_split[iDevice] = std::stof(splitArg[iDevice]);
						} else {
							params.tensor_split[iDevice] = 0.0f;
						}
					}
#else
					LOG_WARNING("llama.cpp was compiled without CUDA. It is not possible to set a tensor split.\n", {});
#endif // GGML_USE_CUDA
				} else if (arg == "--main-gpu" || arg == "-mg") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
#if defined(GGML_USE_CUDA) || defined(GGML_USE_SYCL)
					params.main_gpu = std::stoi(argv[i]);
#endif
				} else if (arg == "--lora") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.lora_adapter.emplace_back(argv[i], 1.0f);
					params.use_mmap = false;
				} else if (arg == "--lora-scaled") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					const char *loraAdapter = argv[i];
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.lora_adapter.emplace_back(loraAdapter, std::stof(argv[i]));
					params.use_mmap = false;
				} else if (arg == "--lora-base") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.lora_base = argv[i];
				} else if (arg == "--mlock") {
					params.use_mlock = true;
				} else if (arg == "--no-mmap") {
					params.use_mmap = false;
				} else if (arg == "--numa") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					} else {
						std::string value(argv[i]);
						/**/ if (value == "distribute" || value.empty()) {
							params.numa = GGML_NUMA_STRATEGY_DISTRIBUTE;
						} else if (value == "isolate") {
							params.numa = GGML_NUMA_STRATEGY_ISOLATE;
						} else if (value == "numactl") {
							params.numa = GGML_NUMA_STRATEGY_NUMACTL;
						} else {
							invalidParam = true; break;
						}
					}
				} else if (arg == "--embedding" || arg == "--embeddings") {
					params.embedding = true;
				} else if (arg == "-cb" || arg == "--cont-batching") {
					params.cont_batching = true;
				} else if (arg == "-fa" || arg == "--flash-attn") {
					params.flash_attn = true;
				} else if (arg == "-np" || arg == "--parallel") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_parallel = std::stoi(argv[i]);
				} else if (arg == "-n" || arg == "--n-predict") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					params.n_predict = std::stoi(argv[i]);
				} else if (arg == "-ctk" || arg == "--cache-type-k") {
					params.cache_type_k = argv[++i];
				} else if (arg == "-ctv" || arg == "--cache-type-v") {
					params.cache_type_v = argv[++i];
				} else if (arg == "--override-kv") {
					if (++i >= argc) {
						invalidParam = true;
						break;
					}
					if (!parse_kv_override(argv[i], params.kv_overrides)) {
						// fprintf(stderr, "error: Invalid type for KV override: %s\n", argv[i]);
						spdlog::error("Invalid type for KV override: {}", argv[i]);
						invalidParam = true;
						break;
					}
				} else {
					// fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
					spdlog::error("Unknown argument: {}", arg);
					// exit(1);
					return false;
				}
			}

			gpt_params_handle_model_default(params);

			if (!params.kv_overrides.empty()) {
				params.kv_overrides.emplace_back();
				params.kv_overrides.back().key[0] = 0;
			}

			if (invalidParam) {
				// fprintf(stderr, "error: invalid parameter for argument: %s\n", arg.c_str());
				spdlog::error("Invalid parameter for argument: {}", arg);
				// exit(1);
				return false;
			}
			return true;
		}

		static std::optional<std::map<std::string, std::string>> loadModelMetadata(const std::string &fname)
		{
			try {
				struct ggml_context *ctxData = nullptr;

				const struct gguf_init_params params = {
					/*.no_alloc = */ false,
					/*.ctx      = */ &ctxData,
				};

				struct gguf_context *ctx = gguf_init_from_file(fname.c_str(), params);

				std::map<std::string, std::string> kv;

				kv["version"] = std::to_string(gguf_get_version(ctx));
				kv["alignment"] = std::to_string(gguf_get_alignment(ctx));
				kv["data offset"] = std::to_string(gguf_get_data_offset(ctx));

				const int count = gguf_get_n_kv(ctx);

				for (int i = 0; i < count; ++i) {
					const char *key = gguf_get_key(ctx, i);
					const auto type = gguf_get_kv_type(ctx, i);

					switch (type) {
						case GGUF_TYPE_UINT8:
							kv[key] = std::to_string(gguf_get_val_u8(ctx, i));
							break;
						case GGUF_TYPE_INT8:
							kv[key] = std::to_string(gguf_get_val_i8(ctx, i));
							break;
						case GGUF_TYPE_UINT16:
							kv[key] = std::to_string(gguf_get_val_u16(ctx, i));
							break;
						case GGUF_TYPE_INT16:
							kv[key] = std::to_string(gguf_get_val_i16(ctx, i));
							break;
						case GGUF_TYPE_UINT32:
							kv[key] = std::to_string(gguf_get_val_u32(ctx, i));
							break;
						case GGUF_TYPE_INT32:
							kv[key] = std::to_string(gguf_get_val_i32(ctx, i));
							break;
						case GGUF_TYPE_UINT64:
							kv[key] = std::to_string(gguf_get_val_u64(ctx, i));
							break;
						case GGUF_TYPE_INT64:
							kv[key] = std::to_string(gguf_get_val_i64(ctx, i));
							break;
						case GGUF_TYPE_FLOAT32:
							kv[key] = std::to_string(gguf_get_val_f32(ctx, i));
							break;
						case GGUF_TYPE_FLOAT64:
							kv[key] = std::to_string(gguf_get_val_f64(ctx, i));
							break;
						case GGUF_TYPE_BOOL:
							kv[key] = std::to_string(gguf_get_val_bool(ctx, i));
							break;
						case GGUF_TYPE_STRING:
							kv[key] = gguf_get_val_str(ctx, i);
							break;
						case GGUF_TYPE_ARRAY:
							// kv_map[key] = std::to_string(gguf_get_val_arr_size(ctx, i));
							kv[key] = "[array]";
							break;
						case GGUF_TYPE_COUNT: // marks the end of the enum
							break;
						default:
							// fprintf(stdout, "'%s' unknown key type: %d\n", key, keyType);
							// spdlog::warn("'{}' unknown key type: {}", key, type);
							break;
					}
				}

				// get model context size if `general.name` and `[model name].context_length` exists
				if (kv.contains("general.architecture")) {
					const auto modelName = util::stringLower(kv["general.architecture"]);
					const auto ctxName = modelName + ".context_length";
					if (kv.contains(ctxName)) {
						kv["context_length"] = kv[ctxName];
					}
				}
				ggml_free(ctxData);
				gguf_free(ctx);

				return kv;
			} catch (const std::exception &e) {
				spdlog::error("Failed to load model: {}", e.what());
				return std::nullopt;
			}
		}

		static std::tuple<llama_model *, llama_context *> loadModel(gpt_params &params, bool didUserSetCtxSize = false)
		{
			llama_backend_init();

			// load the model
			auto [m, ctx] = llama_init_from_gpt_params(params);
			if (m == nullptr) {
				spdlog::error("Unable to load model");
				return { nullptr, nullptr };
			}

			const auto trainingContextLength = llama_n_ctx_train(m);
			const auto contextLength = static_cast<int32_t>(llama_n_ctx(ctx));

			if (contextLength > trainingContextLength) {
				spdlog::warn("Model was trained on only {} context tokens ({} specified)", trainingContextLength, contextLength);
			}

			return { m, ctx };
		}

		static void unloadModel(const std::tuple<llama_model *, llama_context *> &m)
		{
			// clean up
			llama_free(std::get<1>(m));
			llama_free_model(std::get<0>(m));
			llama_backend_free();
		}
		//
		// bool OnInferenceProgressDefault(const nlohmann::json &metrics) { return true; }
		//
		// void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
		// {}
		//
		// void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)
		// {}

		std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
		std::function<void(const std::string &alias, const WingmanItemStatus &status)> onInferenceStatus = nullptr;
		std::function<void(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)> onInferenceServiceStatus = nullptr;
		// std::function<void()> requestShutdownInference = nullptr;
		std::map<std::string, std::string> metadata;
		std::string modelPath;
		gpt_params params;
		bool isInferring = false;
		bool lazyLoadModel = false;
	public:

		ModelLoader(const int argc, char **argv) {
			bool didUserSetCtxSize = false;
			if (parseParams(argc, argv, params, didUserSetCtxSize)) {
#ifndef NDEBUG
				if (params.model.empty() || !std::filesystem::exists(params.model)) {
					const auto home = GetWingmanHome();
					params.model = (home / "models" / std::filesystem::path(DEFAULT_MODEL_FILE).filename().string()).
						string();
				}
#endif
				const auto meta = loadModelMetadata(params.model);
				if (meta) {
					metadata = meta.value();
				} else {
					throw std::runtime_error("Failed to load model metadata");
				}

				// if the user didn't set the context size, use the default from the model
				auto contextLength = DEFAULT_CONTEXT_LENGTH;
				if (!didUserSetCtxSize && metadata.contains("context_length")) {
					contextLength = std::stoi(metadata["context_length"]);
					params.n_ctx = contextLength;
				}

				if (!lazyLoadModel) {
					// load the model
					model = loadModel(params);

					if (std::get<0>(model) == nullptr) {
						throw std::runtime_error("Failed to load model");
					}
					if (std::get<1>(model) == nullptr) {
						throw std::runtime_error("Failed to load model context");
					}
				}
			}
		}

		ModelLoader(std::string modelPath, const std::function<bool(const nlohmann::json &metrics)> &onProgress,
		            const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus,
		            const std::function<void(const wingman::WingmanServiceAppItemStatus &status,
		            std::optional<std::string> error)> &onServiceStatus) :
			onInferenceProgress(onProgress),
			onInferenceStatus(onStatus),
			onInferenceServiceStatus(onServiceStatus),
			
			lazyLoadModel(true),
			modelPath(modelPath)
		{
		}

		~ModelLoader()
		{
			if (!lazyLoadModel)
				unloadModel(model);
		}

		std::tuple<llama_model *, llama_context *> model;

		std::string modelName() const
		{
			if (!metadata.empty() && metadata.contains("general.name"))
				return metadata.at("general.name");
			return "Not Available";
		}

		llama_model *getModel() const
		{
			return std::get<0>(model);
		}

		llama_context *getContext() const
		{
			return std::get<1>(model);
		}

		std::map<std::string, std::string> getMetadata() const
		{
			return metadata;
		}

		static std::optional<std::map<std::string, std::string>> loadMetadata(const std::string &modelPath)
		{
			return loadModelMetadata(modelPath);
		}

		std::string getModelPath() const
		{
			return modelPath;
		}

		int run(const int argc, char **argv, std::function<void()> &requestShutdownInference)
		{
			// gpt_params params;
			bool didUserSetCtxSize = false;
			if (parseParams(argc, argv, params, didUserSetCtxSize)) {
#ifndef NDEBUG
				if (params.model.empty() || !std::filesystem::exists(params.model)) {
					const auto home = GetWingmanHome();
					params.model = (home / "models" / std::filesystem::path(DEFAULT_MODEL_FILE).filename().string()).string();
				}
#endif
				const auto meta = loadModelMetadata(params.model);
				if (meta) {
					metadata = meta.value();
				} else {
					throw std::runtime_error("Failed to load model metadata");
				}

				// if the user didn't set the context size, use the default from the model
				auto contextLength = DEFAULT_CONTEXT_LENGTH;
				if (!didUserSetCtxSize && metadata.contains("context_length")) {
					contextLength = std::stoi(metadata["context_length"]);
					params.n_ctx = contextLength;
				}
				return run_inference(argc, argv, requestShutdownInference, onInferenceProgress, onInferenceStatus, onInferenceServiceStatus);
			}
			return -1;
		}
	};

	class ModelGenerator {
		ModelLoader &loader;

	public:
		explicit ModelGenerator(ModelLoader &loader) : loader(loader)
		{}

		ModelGenerator(const int argc, char **argv) : loader(*new ModelLoader(argc, argv))
		{}

		std::optional<std::vector<llama_token>> tokenize(const std::string &text, int maxTokensToGenerate) const
		{
			// const auto model = loader.getModel();
			const auto context = loader.getContext();

			std::vector<llama_token> tokensList = ::llama_tokenize(context, text, true);

			const auto contextLength = llama_n_ctx(context);
			const auto totalLengthRequired = tokensList.size() + (maxTokensToGenerate - tokensList.size());

			spdlog::trace("{}: n_len = {}, n_ctx = {}, n_kv_req = {}", __func__, maxTokensToGenerate, contextLength, totalLengthRequired);

			// make sure the KV cache is big enough to hold all the prompt and generated tokens
			if (totalLengthRequired > contextLength) {
				spdlog::error("The required KV cache size is not big enough either reduce totalLength or increase contextLength");
				return std::nullopt;
			}
			return tokensList;
		}

		using token_callback = std::function<void(const std::string &)>;

		void generate(const gpt_params &params, const int& maxTokensToGenerate, const token_callback &onNewToken, const std::atomic<bool> &cancelled) const
		{
			const auto model = loader.getModel();
			const auto context = loader.getContext();

			const auto tokensList = tokenize(params.prompt, maxTokensToGenerate);
			if (!tokensList) {
				spdlog::error("Failed to tokenize the prompt");
				throw std::runtime_error("Failed to tokenize the prompt");
			}

			llama_batch batch = llama_batch_init(params.n_batch, 0, 1);
			for (llama_pos i = 0; i < tokensList.value().size(); i++) {
				llama_batch_add(batch, tokensList.value()[i], i, { 0 }, false);
			}
			batch.logits[batch.n_tokens - 1] = true;

			if (llama_decode(context, batch) != 0) {
				spdlog::error("llama_decode() failed");
				throw std::runtime_error("llama_decode() failed");
			}

			int currentTokenBatchCount = batch.n_tokens;
			int generatedTokensCount = 0;

			const auto startTime = ggml_time_us();

			while (currentTokenBatchCount <= maxTokensToGenerate) {
				// Check for cancellation
				if (cancelled.load(std::memory_order_relaxed)) {
					spdlog::info("Generation cancelled");
					break;
				}

				const auto vocabLength = llama_n_vocab(model);
				const auto *logits = llama_get_logits_ith(context, batch.n_tokens - 1);

				std::vector<llama_token_data> candidates;
				candidates.reserve(vocabLength);
				for (llama_token tokenId = 0; tokenId < vocabLength; tokenId++) {
					candidates.emplace_back(llama_token_data{ tokenId, logits[tokenId], 0.0f });
				}

				llama_token_data_array candidatesP = { candidates.data(), candidates.size(), false };
				const llama_token newTokenId = llama_sample_token_greedy(context, &candidatesP);
				if (llama_token_is_eog(model, newTokenId) || currentTokenBatchCount >= maxTokensToGenerate) {
					break;
				}

				const auto newToken = llama_token_to_piece(context, newTokenId);
				onNewToken(newToken);

				llama_batch_clear(batch);
				llama_batch_add(batch, newTokenId, currentTokenBatchCount, { 0 }, true);

				generatedTokensCount += 1;
				currentTokenBatchCount += 1;

				if (llama_decode(context, batch)) {
					spdlog::error("Failed to evaluate the current batch");
					throw std::runtime_error("Failed to evaluate the current batch");
				}
			}

			const auto endTime = ggml_time_us();
			const auto duration = endTime - startTime;
			spdlog::info("Generated {} tokens in {} ms", generatedTokensCount, duration / 1000);
			llama_batch_free(batch);
		}

		std::string modelName() const
		{
			return metadata()["general.name"];
		}

		std::map<std::string, std::string> metadata() const
		{
			return loader.getMetadata();
		}
	};
} // namespace wingman
