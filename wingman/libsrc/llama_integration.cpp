#include <spdlog/spdlog.h>

#include "llama_integration.h"

namespace wingman::silk {
	ModelLoader::ModelLoader(const int argc, char **argv)
	{
		bool didUserSetCtxSize = false;
		if (parseServerParams(argc, argv, sparams, params, didUserSetCtxSize)) {
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
			if (!didUserSetCtxSize && metadata.contains("context_length")) {
				params.n_ctx = std::stoi(metadata["context_length"]);
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

	ModelLoader::ModelLoader(const std::string &model,
			const std::function<bool(const nlohmann::json &metrics)> &onProgress,
			const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus,
			const std::function<void(const wingman::WingmanServiceAppItemStatus &status,
				std::optional<std::string> error)> &onServiceStatus) :
		onInferenceProgress(onProgress),
		onInferenceStatus(onStatus),
		onInferenceServiceStatus(onServiceStatus),

		modelPath(model),
		lazyLoadModel(true)
	{
		if (model.empty()) {
			throw std::runtime_error("Model file parameter is empty");
		}
		const auto [modelRepo, filePath] = parseModelFromMoniker(model);
		this->modelPath = orm::DownloadItemActions::getDownloadItemOutputPath(modelRepo, filePath);
		if (!std::filesystem::exists(this->modelPath)) {
			// check if the model file exists in the models directory
			const auto home = GetWingmanHome();
			const auto file = (home / "models" / std::filesystem::path(model).filename().string()).string();
			if (std::filesystem::exists(file)) {
				this->modelPath = file;
			} else {
				throw std::runtime_error("Model file does not exist");
			}
		}
		const auto meta = loadModelMetadata(this->modelPath);
		if (meta) {
			metadata = meta.value();
		} else {
			throw std::runtime_error("Failed to load model metadata");
		}

		if (!metadata.contains("context_length")) {
			const auto contextName = metadata["general.architecture"] + ".context_length";
			metadata["context_length"] = metadata[contextName];
		}
	}

	bool ModelLoader::parseServerParams(
		int argc, char **argv, server_params &sparams, gpt_params &params, bool &didUserSetCtxSize)
	{
		gpt_params    default_params;
		server_params default_sparams;

		std::string arg;
		bool invalid_param = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--port") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.port = std::stoi(argv[i]);
			} else if (arg == "--host") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.hostname = argv[i];
			} else if (arg == "--path") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.public_path = argv[i];
			} else if (arg == "--api-key") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.api_keys.push_back(argv[i]);
			} else if (arg == "--api-key-file") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				std::ifstream key_file(argv[i]);
				if (!key_file) {
					fprintf(stderr, "error: failed to open file '%s'\n", argv[i]);
					invalid_param = true;
					break;
				}
				std::string key;
				while (std::getline(key_file, key)) {
					if (key.size() > 0) {
						sparams.api_keys.push_back(key);
					}
				}
				key_file.close();

			}
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
			else if (arg == "--ssl-key-file") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.ssl_key_file = argv[i];
			} else if (arg == "--ssl-cert-file") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.ssl_cert_file = argv[i];
			}
#endif
			else if (arg == "--timeout" || arg == "-to") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.read_timeout = std::stoi(argv[i]);
				sparams.write_timeout = std::stoi(argv[i]);
			} else if (arg == "-m" || arg == "--model") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.model = argv[i];
			} else if (arg == "-mu" || arg == "--model-url") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.model_url = argv[i];
			} else if (arg == "-hfr" || arg == "--hf-repo") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.hf_repo = argv[i];
			} else if (arg == "-hff" || arg == "--hf-file") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.hf_file = argv[i];
			} else if (arg == "-a" || arg == "--alias") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.model_alias = argv[i];
				// } else if (arg == "-h" || arg == "--help") {
				// 	server_print_usage(argv[0], default_params, default_sparams);
				// 	exit(0);
			} else if (arg == "-c" || arg == "--ctx-size" || arg == "--ctx_size") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_ctx = std::stoi(argv[i]);
				didUserSetCtxSize = true;
			} else if (arg == "--rope-scaling") {
				if (++i >= argc) {
					invalid_param = true;
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
					invalid_param = true; break;
				}
			} else if (arg == "--rope-freq-base") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.rope_freq_base = std::stof(argv[i]);
			} else if (arg == "--rope-freq-scale") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.rope_freq_scale = std::stof(argv[i]);
			} else if (arg == "--yarn-ext-factor") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.yarn_ext_factor = std::stof(argv[i]);
			} else if (arg == "--yarn-attn-factor") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.yarn_attn_factor = std::stof(argv[i]);
			} else if (arg == "--yarn-beta-fast") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.yarn_beta_fast = std::stof(argv[i]);
			} else if (arg == "--yarn-beta-slow") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.yarn_beta_slow = std::stof(argv[i]);
			} else if (arg == "--pooling") {
				if (++i >= argc) {
					invalid_param = true;
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
					invalid_param = true; break;
				}
			} else if (arg == "--defrag-thold" || arg == "-dt") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.defrag_thold = std::stof(argv[i]);
			} else if (arg == "--threads" || arg == "-t") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_threads = std::stoi(argv[i]);
			} else if (arg == "--grp-attn-n" || arg == "-gan") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}

				params.grp_attn_n = std::stoi(argv[i]);
			} else if (arg == "--grp-attn-w" || arg == "-gaw") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}

				params.grp_attn_w = std::stoi(argv[i]);
			} else if (arg == "--threads-batch" || arg == "-tb") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_threads_batch = std::stoi(argv[i]);
			} else if (arg == "--threads-http") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.n_threads_http = std::stoi(argv[i]);
			} else if (arg == "-b" || arg == "--batch-size") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_batch = std::stoi(argv[i]);
			} else if (arg == "-ub" || arg == "--ubatch-size") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_ubatch = std::stoi(argv[i]);
			} else if (arg == "--gpu-layers" || arg == "-ngl" || arg == "--n-gpu-layers") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				if (llama_supports_gpu_offload()) {
					params.n_gpu_layers = std::stoi(argv[i]);
				} else {
					// spdlog::warn("Not compiled with GPU offload support, --n-gpu-layers option will be ignored.");
				}
			} else if (arg == "-nkvo" || arg == "--no-kv-offload") {
				params.no_kv_offload = true;
			} else if (arg == "--split-mode" || arg == "-sm") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				std::string arg_next = argv[i];
				if (arg_next == "none") {
					params.split_mode = LLAMA_SPLIT_MODE_NONE;
				} else if (arg_next == "layer") {
					params.split_mode = LLAMA_SPLIT_MODE_LAYER;
				} else if (arg_next == "row") {
					params.split_mode = LLAMA_SPLIT_MODE_ROW;
				} else {
					invalid_param = true;
					break;
				}
#ifndef GGML_USE_CUDA
				fprintf(stderr, "warning: llama.cpp was compiled without CUDA. Setting the split mode has no effect.\n");
#endif // GGML_USE_CUDA
			} else if (arg == "--tensor-split" || arg == "-ts") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
#if defined(GGML_USE_CUDA) || defined(GGML_USE_SYCL)
				std::string arg_next = argv[i];

				// split string by , and /
				const std::regex regex{ R"([,/]+)" };
				std::sregex_token_iterator it{ arg_next.begin(), arg_next.end(), regex, -1 };
				std::vector<std::string> split_arg{ it, {} };
				GGML_ASSERT(split_arg.size() <= llama_max_devices());

				for (size_t i_device = 0; i_device < llama_max_devices(); ++i_device) {
					if (i_device < split_arg.size()) {
						params.tensor_split[i_device] = std::stof(split_arg[i_device]);
					} else {
						params.tensor_split[i_device] = 0.0f;
					}
				}
#else
				LOG_WARNING("llama.cpp was compiled without CUDA. It is not possible to set a tensor split.\n", {});
#endif // GGML_USE_CUDA
			} else if (arg == "--main-gpu" || arg == "-mg") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
#if defined(GGML_USE_CUDA) || defined(GGML_USE_SYCL)
				params.main_gpu = std::stoi(argv[i]);
#else
				LOG_WARNING("llama.cpp was compiled without CUDA. It is not possible to set a main GPU.", {});
#endif
			} else if (arg == "--lora") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.lora_adapter.emplace_back(argv[i], 1.0f);
				params.use_mmap = false;
			} else if (arg == "--lora-scaled") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				const char *lora_adapter = argv[i];
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.lora_adapter.emplace_back(lora_adapter, std::stof(argv[i]));
				params.use_mmap = false;
			} else if (arg == "--lora-base") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.lora_base = argv[i];
			} else if (arg == "-v" || arg == "--verbose") {
#if SERVER_VERBOSE != 1
				spdlog::warn("server.cpp is not built with verbose logging.");
#else
				server_verbose = true;
#endif
			} else if (arg == "--mlock") {
				params.use_mlock = true;
			} else if (arg == "--no-mmap") {
				params.use_mmap = false;
			} else if (arg == "--numa") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				} else {
					std::string value(argv[i]);
					/**/ if (value == "distribute" || value == "") {
						params.numa = GGML_NUMA_STRATEGY_DISTRIBUTE;
					} else if (value == "isolate") {
						params.numa = GGML_NUMA_STRATEGY_ISOLATE;
					} else if (value == "numactl") {
						params.numa = GGML_NUMA_STRATEGY_NUMACTL;
					} else {
						invalid_param = true; break;
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
					invalid_param = true;
					break;
				}
				params.n_parallel = std::stoi(argv[i]);
			} else if (arg == "-n" || arg == "--n-predict") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				params.n_predict = std::stoi(argv[i]);
			} else if (arg == "-spf" || arg == "--system-prompt-file") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				std::ifstream file(argv[i]);
				if (!file) {
					fprintf(stderr, "error: failed to open file '%s'\n", argv[i]);
					invalid_param = true;
					break;
				}
				std::string system_prompt;
				std::copy(
					std::istreambuf_iterator<char>(file),
					std::istreambuf_iterator<char>(),
					std::back_inserter(system_prompt)
				);
				sparams.system_prompt = system_prompt;
			} else if (arg == "-ctk" || arg == "--cache-type-k") {
				params.cache_type_k = argv[++i];
			} else if (arg == "-ctv" || arg == "--cache-type-v") {
				params.cache_type_v = argv[++i];
			} else if (arg == "--log-format") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				// if (std::strcmp(argv[i], "json") == 0) {
				// 	server_log_json = true;
				// } else if (std::strcmp(argv[i], "text") == 0) {
				// 	server_log_json = false;
				// } else {
				// 	invalid_param = true;
				// 	break;
				// }
			} else if (arg == "--log-disable") {
				log_set_target(stdout);
				// spdlog::info("logging to file is disabled.");
			} else if (arg == "--slots-endpoint-disable") {
				sparams.slots_endpoint = false;
			} else if (arg == "--metrics") {
				sparams.metrics_endpoint = true;
			} else if (arg == "--slot-save-path") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				sparams.slot_save_path = argv[i];
				// if doesn't end with DIRECTORY_SEPARATOR, add it
				if (!sparams.slot_save_path.empty() && sparams.slot_save_path[sparams.slot_save_path.size() - 1] != DIRECTORY_SEPARATOR) {
					sparams.slot_save_path += DIRECTORY_SEPARATOR;
				}
			} else if (arg == "--chat-template") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				// if (!verify_custom_template(argv[i])) {
				// 	fprintf(stderr, "error: the supplied chat template is not supported: %s\n", argv[i]);
				// 	fprintf(stderr, "note: llama.cpp does not use jinja parser, we only support commonly used templates\n");
				// 	invalid_param = true;
				// 	break;
				// }
				sparams.chat_template = argv[i];
			} else if (arg == "--override-kv") {
				if (++i >= argc) {
					invalid_param = true;
					break;
				}
				if (!string_parse_kv_override(argv[i], params.kv_overrides)) {
					// spdlog::error("error: Invalid type for KV override: {}\n", argv[i]);
					invalid_param = true;
					break;
				}
			} else {
				// spdlog::error("error: unknown argument: {}\n", arg.c_str());
				// server_print_usage(argv[0], default_params, default_sparams);
				// exit(1);
				return false;
			}
		}

		gpt_params_handle_model_default(params);

		if (!params.kv_overrides.empty()) {
			params.kv_overrides.emplace_back();
			params.kv_overrides.back().key[0] = 0;
		}

		if (invalid_param) {
			spdlog::error("error: invalid parameter for argument: {}\n", arg.c_str());
			// server_print_usage(argv[0], default_params, default_sparams);
			// exit(1);
		}
		return true;
	}

	bool ModelLoader::parseGptParams(int argc, char **argv, gpt_params &params, bool &didUserSetCtxSize)
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
				if (!string_parse_kv_override(argv[i], params.kv_overrides)) {
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

	std::string ModelLoader::llama_model_ftype_name(llama_ftype ftype)
	{
		if (ftype & LLAMA_FTYPE_GUESSED) {
			return llama_model_ftype_name(static_cast<enum llama_ftype>(ftype & ~LLAMA_FTYPE_GUESSED)) + " (guessed)";
		}

		switch (ftype) {
			case LLAMA_FTYPE_ALL_F32:     return "all F32";
			case LLAMA_FTYPE_MOSTLY_F16:  return "F16";
			case LLAMA_FTYPE_MOSTLY_Q4_0: return "Q4_0";
			case LLAMA_FTYPE_MOSTLY_Q4_1: return "Q4_1";
			case LLAMA_FTYPE_MOSTLY_Q4_1_SOME_F16:
				return "Q4_1, some F16";
			case LLAMA_FTYPE_MOSTLY_Q5_0: return "Q5_0";
			case LLAMA_FTYPE_MOSTLY_Q5_1: return "Q5_1";
			case LLAMA_FTYPE_MOSTLY_Q8_0: return "Q8_0";

			// K-quants
			case LLAMA_FTYPE_MOSTLY_Q2_K:   return "Q2_K - Medium";
			case LLAMA_FTYPE_MOSTLY_Q2_K_S: return "Q2_K - Small";
			case LLAMA_FTYPE_MOSTLY_Q3_K_S: return "Q3_K - Small";
			case LLAMA_FTYPE_MOSTLY_Q3_K_M: return "Q3_K - Medium";
			case LLAMA_FTYPE_MOSTLY_Q3_K_L: return "Q3_K - Large";
			case LLAMA_FTYPE_MOSTLY_Q4_K_S: return "Q4_K - Small";
			case LLAMA_FTYPE_MOSTLY_Q4_K_M: return "Q4_K - Medium";
			case LLAMA_FTYPE_MOSTLY_Q5_K_S: return "Q5_K - Small";
			case LLAMA_FTYPE_MOSTLY_Q5_K_M: return "Q5_K - Medium";
			case LLAMA_FTYPE_MOSTLY_Q6_K:   return "Q6_K";
			case LLAMA_FTYPE_MOSTLY_IQ2_XXS:return "IQ2_XXS - 2.0625 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ2_XS: return "IQ2_XS - 2.3125 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ2_S:  return "IQ2_S - 2.5 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ2_M:  return "IQ2_M - 2.7 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ3_XS: return "IQ3_XS - 3.3 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ3_XXS:return "IQ3_XXS - 3.0625 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ1_S:return "IQ1_S - 1.5625 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ1_M:return "IQ1_M - 1.75 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ4_NL: return "IQ4_NL - 4.5 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ4_XS: return "IQ4_XS - 4.25 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ3_S:  return "IQ3_S - 3.4375 bpw";
			case LLAMA_FTYPE_MOSTLY_IQ3_M:  return "IQ3_S mix - 3.66 bpw";

			default: return "unknown, may not work";
		}
	}

	void ModelLoader::replace_all(std::string &s, const std::string &search, const std::string &replace)
	{
		std::string result;
		for (size_t pos = 0; ; pos += search.length()) {
			auto new_pos = s.find(search, pos);
			if (new_pos == std::string::npos) {
				result += s.substr(pos, s.size() - pos);
				break;
			}
			result += s.substr(pos, new_pos - pos) + replace;
			pos = new_pos;
		}
		s = std::move(result);
	}

	std::string ModelLoader::gguf_data_to_str(gguf_type type, const void *data, int i)
	{
		switch (type) {
			case GGUF_TYPE_UINT8:   return std::to_string(static_cast<const uint8_t *>(data)[i]);
			case GGUF_TYPE_INT8:    return std::to_string(static_cast<const int8_t *>(data)[i]);
			case GGUF_TYPE_UINT16:  return std::to_string(static_cast<const uint16_t *>(data)[i]);
			case GGUF_TYPE_INT16:   return std::to_string(static_cast<const int16_t *>(data)[i]);
			case GGUF_TYPE_UINT32:  return std::to_string(static_cast<const uint32_t *>(data)[i]);
			case GGUF_TYPE_INT32:   return std::to_string(static_cast<const int32_t *>(data)[i]);
			case GGUF_TYPE_UINT64:  return std::to_string(static_cast<const uint64_t *>(data)[i]);
			case GGUF_TYPE_INT64:   return std::to_string(static_cast<const int64_t *>(data)[i]);
			case GGUF_TYPE_FLOAT32: return std::to_string(static_cast<const float *>(data)[i]);
			case GGUF_TYPE_FLOAT64: return std::to_string(static_cast<const double *>(data)[i]);
			case GGUF_TYPE_BOOL:    return static_cast<const bool *>(data)[i] ? "true" : "false";
			default:                return "???";
		}
	}

	std::optional<std::map<std::string, std::string>> ModelLoader::loadModelMetadata(const std::string &fname)
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
					{
						const enum gguf_type arr_type = gguf_get_arr_type(ctx, i);
						const int arr_n = gguf_get_arr_n(ctx, i);
						const void *data = gguf_get_arr_data(ctx, i);
						std::stringstream ss;
						ss << "[";
						for (int j = 0; j < arr_n; j++) {
							if (arr_type == GGUF_TYPE_STRING) {
								std::string val(gguf_get_arr_str(ctx, i, j));
								// escape quotes
								replace_all(val, "\\", "\\\\");
								replace_all(val, "\"", "\\\"");
								ss << '"' << val << '"';
							} else if (arr_type == GGUF_TYPE_ARRAY) {
								ss << "???";
							} else {
								std::string val(gguf_data_to_str(arr_type, data, j));
								ss << val;
							}
							if (j < arr_n - 1) {
								ss << ", ";
							}
						}
						ss << "]";
						kv[key] = ss.str();
					}
					// kv[key] = "[array]";
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
			if (kv.contains("general.file_type")) {
				const llama_ftype ftype = static_cast<llama_ftype>(std::stoi(kv["general.file_type"]));
				kv["quantization"] = llama_model_ftype_name(ftype);
			}
			if (kv.contains("tokenizer.ggml.tokens")) {
				// strip first and last character (`[`, `]`)
				// split by `,`
				// find all keys that start with `tokenizer.ggml.` and end with `_token_id`,
				//	and replace the value with the corresponding token
				std::string tokens = kv["tokenizer.ggml.tokens"];
				tokens = tokens.substr(1, tokens.size() - 2);
				std::vector<std::string> tokenList = util::splitString(tokens, ',');
				std::map<std::string, int> tokenIndices;
				for (const auto &[f, s] : kv) {
					if (f.starts_with("tokenizer.ggml.") && f.ends_with("_token_id")) {
						tokenIndices[f] = std::atoi(s.c_str());
					}
				}
				for (const auto &[f, s] : tokenIndices) {
					auto raw = tokenList[s];
					const auto token = util::stringTrim(raw).substr(1, raw.size() - 2);
					kv[f] = token;
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

	std::tuple<llama_model *, llama_context *> ModelLoader::loadModel(gpt_params &params, bool didUserSetCtxSize)
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

	void ModelLoader::unloadModel(const std::tuple<llama_model *, llama_context *> &m)
	{
// clean up
		llama_free(std::get<1>(m));
		llama_free_model(std::get<0>(m));
		llama_backend_free();
	}

	std::tuple<std::string, std::string> ModelLoader::parseModelFromMoniker(const std::string &moniker)
	{
		// A model moniker has two formats:
		//  Format 1 contains `[-]` and `[=]` - this is the format used by the model downloader
		//  Format 2 contains `/` - this is the format used for passing the model name and file path as a single string
		//
		//  When Format 1 is used, extract the model name from the file name
		//  When Format 2 is used, search for the model in the downloads db
		std::string modelRepo;
		std::string filePath;
		if (moniker.find("[-]") != std::string::npos && moniker.find("[=]") != std::string::npos) {
			const auto din = orm::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(moniker);
			if (!din.has_value()) {
				throw std::runtime_error("Invalid model name format");
			}
			modelRepo = din->modelRepo;
			filePath = din->filePath;
		} else if (moniker.find('/') != std::string::npos) {
			// split the model name by `/`
			//   the first two parts make up the modelRepo
			//   the last part makes up the filePath
			const auto parts = util::splitString(moniker, '/');
			if (parts.size() != 3) {
				throw std::runtime_error("Invalid model name format");
			}
			modelRepo = parts[0] + "/" + parts[1];
			filePath = parts[2];
		}
		return { modelRepo, filePath };
	}

	std::string ModelLoader::modelName() const
	{
		if (!metadata.empty() && metadata.contains("general.name"))
			return metadata.at("general.name");
		return "Not Available";
	}

	llama_model *ModelLoader::getModel() const
	{
		return std::get<0>(model);
	}

	llama_context *ModelLoader::getContext() const
	{
		return std::get<1>(model);
	}

	std::map<std::string, std::string> ModelLoader::getMetadata() const
	{
		return metadata;
	}

	std::optional<std::map<std::string, std::string>> ModelLoader::loadMetadata(const std::string &modelPath)
	{
		return loadModelMetadata(modelPath);
	}

	std::string ModelLoader::getModelPath() const
	{
		return modelPath;
	}

	int ModelLoader::run(const int argc, char **argv, std::function<void()> &requestShutdownInference) const
	{
		return run_inference(argc, argv, requestShutdownInference, onInferenceProgress, onInferenceStatus, onInferenceServiceStatus);
	}

	ModelGenerator::ModelGenerator(ModelLoader &loader): loader(loader) {}

	ModelGenerator::ModelGenerator(const int argc, char **argv): loader(*new ModelLoader(argc, argv)) {}

	std::optional<std::vector<llama_token>> ModelGenerator::tokenize(
		const std::string &text, int maxTokensToGenerate) const {
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

	void ModelGenerator::generate(
		const gpt_params &params, const int &maxTokensToGenerate, const token_callback &onNewToken,
		const std::atomic<bool> &cancelled) const {
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

	std::string ModelGenerator::modelName() const {
		return metadata()["general.name"];
	}

	std::map<std::string, std::string> ModelGenerator::metadata() const {
		return loader.getMetadata();
	}

	ModelLoader::~ModelLoader()
	{
		if (!lazyLoadModel)
			unloadModel(model);
	}

} // namespace wingman
