
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "curl.h"
#include "download.service.h"
#include "opencl.info.h"

static void llama_log_callback_wingman(ggml_log_level level, const char *text, void *user_data)
{
	// let's write code to extract relevant information from `text` using
	// std::regex
	std::string str(text);
	llama_server_context *ctx = static_cast<llama_server_context *>(user_data);

	if (ctx == nullptr) {
		std::cout << "ctx is nullptr" << std::endl;
		return;
	}

	// llm_load_tensors: ggml ctx size =    0.09 MB
	std::regex ctx_size_regex("llm_load_tensors: ggml ctx size =\\s+(\\d+\\.\\d+) MB");
	std::smatch ctx_size_match;
	static float ctx_size = -1.0;
	if (std::regex_search(str, ctx_size_match, ctx_size_regex)) {
		std::string ctx_size_str = ctx_size_match[1];
		ctx_size = std::stof(ctx_size_str);
		ctx->ctx_size = ctx_size;
		std::cout << "ctx_size: " << ctx_size << std::endl;
	}

	// llm_load_tensors: using CUDA for GPU acceleration
	std::regex using_cuda_regex("llm_load_tensors: using (\\w+) for GPU acceleration");
	std::smatch using_cuda_match;
	static std::string cuda_str;
	if (std::regex_search(str, using_cuda_match, using_cuda_regex)) {
		cuda_str = using_cuda_match[1];
		ctx->cuda_str = cuda_str;
		std::cout << "cuda_str: " << cuda_str << std::endl;
	}

	// llm_load_tensors: mem required  =   70.44 MB
	std::regex mem_required_regex("llm_load_tensors: mem required  =\\s+(\\d+\\.\\d+)\\s+(\\w+)");
	std::smatch mem_required_match;
	static float mem_required = -1.0;
	if (std::regex_search(str, mem_required_match, mem_required_regex)) {
		std::string mem_required_str = mem_required_match[1];
		std::string mem_required_unit = mem_required_match[2];
		mem_required = std::stof(mem_required_str);
		ctx->mem_required = mem_required;
		ctx->mem_required_unit = mem_required_unit;
		std::cout << "mem_required: " << mem_required << " " << mem_required_unit << std::endl;
	}

	// llm_load_tensors: offloading 32 repeating layers to GPU
	std::regex offloading_repeating_regex("llm_load_tensors: offloading (\\d+) repeating layers to GPU");
	std::smatch offloading_repeating_match;
	static int offloading_repeating = -1;
	if (std::regex_search(str, offloading_repeating_match, offloading_repeating_regex)) {
		std::string offloading_repeating_str = offloading_repeating_match[1];
		offloading_repeating = std::stoi(offloading_repeating_str);
		ctx->offloading_repeating = offloading_repeating;
		std::cout << "repeating layers offloaded: " << offloading_repeating << std::endl;
	}

	// llm_load_tensors: offloading non-repeating layers to GPU
	std::regex offloading_nonrepeating_regex("llm_load_tensors: offloading (\\d+) non-repeating layers to GPU");
	std::smatch offloading_nonrepeating_match;
	static int offloading = -1;
	if (std::regex_search(str, offloading_nonrepeating_match, offloading_nonrepeating_regex)) {
		std::string offloading_str = offloading_nonrepeating_match[1];
		offloading = std::stoi(offloading_str);
		ctx->offloading_nonrepeating = offloading;
		std::cout << "non-repeating layers offloaded: " << offloading << std::endl;
	}

	// llm_load_tensors: offloaded 35/35 layers to GPU
	std::regex offloaded_regex("llm_load_tensors: offloaded (\\d+)/(\\d+) layers to GPU");
	std::smatch offloaded_match;
	static int offloaded = -1;
	static int offloaded_total = -1;
	if (std::regex_search(str, offloaded_match, offloaded_regex)) {
		std::string offloaded_str = offloaded_match[1];
		std::string offloaded_total_str = offloaded_match[2];
		offloaded = std::stoi(offloaded_str);
		ctx->offloaded = offloaded;
		offloaded_total = std::stoi(offloaded_total_str);
		ctx->offloaded_total = offloaded_total;
		std::cout << "offloaded: " << offloaded << "/" << offloaded_total << std::endl;
	}

	// llm_load_tensors: VRAM used: 4849 MB
	std::regex vram_used_regex("llm_load_tensors: VRAM used: (\\d+.\\d+) MB");
	std::smatch vram_used_match;
	static float vram_used = -1.0;
	static float vram_per_layer_avg = -1.0;
	if (std::regex_search(str, vram_used_match, vram_used_regex)) {
		std::string vram_used_str = vram_used_match[1];
		vram_used = std::stof(vram_used_str);
		ctx->vram_used = vram_used;
		vram_per_layer_avg = vram_used / static_cast<float>(offloaded_total);
		ctx->vram_per_layer_avg = vram_per_layer_avg;
		std::cout << "vram_used: " << vram_used << std::endl;
		std::cout << "vram_per_layer_avg: " << vram_per_layer_avg << std::endl;
	}

	// llama_model_loader: - type  f32:   65 tensors
	// llama_model_loader: - type  f16:    1 tensors
	// llama_model_loader: - type q4_0:    1 tensors
	// llama_model_loader: - type q2_K:   64 tensors
	// llama_model_loader: - type q3_K:  160 tensors
	std::regex type_regex("llama_model_loader: - type\\s+(\\w+):\\s+(\\d+) tensors");
	std::smatch tensor_type_match;
	static std::map<std::string, int> tensor_type_map;
	if (std::regex_search(str, tensor_type_match, type_regex)) {
		std::string tensor_type_str = tensor_type_match[1];
		std::string tensor_count_str = tensor_type_match[2];
		int tensor_count = std::stoi(tensor_count_str);
		tensor_type_map[tensor_type_str] = tensor_count;
		ctx->tensor_type_map[tensor_type_str] = tensor_count;
		std::cout << "tensor_type: " << tensor_type_str << " " << tensor_count << std::endl;
	}

	// llm_load_print_meta: format         = GGUF V1 (support until nov 2023)
	// llm_load_print_meta: arch           = llama
	std::regex meta_regex("llm_load_print_meta: (\\w+)\\s+=\\s+(.+)");
	std::smatch meta_match;
	static std::map<std::string, std::string> meta_map;
	if (std::regex_search(str, meta_match, meta_regex)) {
		std::string meta_key_str = meta_match[1];
		std::string meta_value_str = meta_match[2];
		meta_map[meta_key_str] = meta_value_str;
		ctx->meta_map[meta_key_str] = meta_value_str;
		std::cout << "meta_key: " << meta_key_str << " " << meta_value_str << std::endl;
	}

	(void)level;
	(void)user_data;
}

static json format_timing_report(llama_server_context &llama)
{
	const auto timings = llama_get_timings(llama.ctx);

	const auto time = wingman::util::now();

	const json tensor_type_json = llama.tensor_type_map;
	const json meta_json = llama.meta_map;

	const json timings_json = json{
		{"timestamp", time},
		{"load_time", timings.t_load_ms},
		{"sample_time", timings.t_sample_ms},
		{"sample_count", timings.n_sample},
		{"sample_per_token_ms", timings.t_sample_ms / timings.n_sample},
		{"sample_per_second", 1e3 / timings.t_sample_ms * timings.n_sample},
		{"total_time", (timings.t_end_ms - timings.t_start_ms)},

		{"prompt_count", timings.n_p_eval},
		{"prompt_ms", timings.t_p_eval_ms},
		{"prompt_per_token_ms", timings.t_p_eval_ms / timings.n_p_eval},
		{"prompt_per_second", 1e3 / timings.t_p_eval_ms * timings.n_p_eval},

		{"predicted_count", timings.n_eval},
		{"predicted_ms", timings.t_eval_ms},
		{"predicted_per_token_ms", timings.t_eval_ms / timings.n_eval},
		{"predicted_per_second", 1e3 / timings.t_eval_ms * timings.n_eval},
	};

	const auto platforms = getCLPlatformDevices();
	std::string gpuName = getGPUName();

	const auto model_file_name = std::filesystem::path(llama.params.model).stem().string();
	const auto downloadItemName = wingman::orm::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(model_file_name);
	std::string model_name = model_file_name;
	std::string quantization = "?";
	if (downloadItemName.has_value()) {
		model_name = downloadItemName.value().modelRepo;
		quantization = downloadItemName.value().quantization;
	}
	const json system_json = json{ {"ctx_size", llama.n_ctx},
								  {"cuda_str", llama.cuda_str},
								  {"gpu_name", gpuName},
								  {"mem_required", llama.mem_required},
								  {"offloading_repeating", llama.offloading_repeating},
								  {"offloading_nonrepeating", llama.offloading_nonrepeating},
								  {"offloaded", llama.offloaded},
								  {"offloaded_total", llama.offloaded_total},
								  {"vram_used", llama.vram_used},
								  {"vram_per_layer_avg", llama.vram_per_layer_avg},
								  {"model_path", llama.params.model},
								  {"model_file_name", model_file_name},
								  {"model_name", model_name},
								  {"model_alias", llama.params.model_alias},
								  {"quantization", quantization},
								  {"has_next_token", llama.has_next_token} };

	return json{
		{"alias", llama.params.model_alias},
		{"meta", meta_json},
		{"system", system_json},
		{"tensors", tensor_type_json},
		{"timings", timings_json},
	};
}

Server svr;
//llama_server_context *globalLlamaContext; // ref to current context global, to satisfy us_timer function ptr  🤢
//wingman::ItemActionsFactory actions;
bool keepRunning = true;
wingman::WingmanItemStatus lastStatus = wingman::WingmanItemStatus::unknown;

std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> onInferenceStatus = nullptr;

void metrics_reporting_thread(llama_server_context &llama)
{
	while (keepRunning) {
		std::chrono::milliseconds update_interval(1000);
		if (onInferenceProgress != nullptr) {
			const auto kr = onInferenceProgress(format_timing_report(llama));
			if (!kr)
				return;
			if (llama.has_next_token) {
				update_interval = std::chrono::milliseconds(250);
			} else {
				update_interval = std::chrono::milliseconds(1000);
			}
		}
		//if (onInferenceStatus != nullptr) {
		//	const auto kr = onInferenceStatus(lastStatus);
		//}
		std::this_thread::sleep_for(update_interval);
	}
}

void update_inference_status(const std::string &alias, const wingman::WingmanItemStatus &status)
{
	lastStatus = status;
	if (onInferenceStatus != nullptr) {
		onInferenceStatus(alias, status);
	}
}

void extraLlamaContextInit(llama_server_context &llama)
{
	//globalLlamaContext = &llama;
	// miscelaneous info gathered from model loading
	float ctx_size = -1.0;
	std::string cuda_str;
	float mem_required = -1.0;
	std::string mem_required_unit;
	int offloading_repeating = -1;
	int offloading_nonrepeating = -1;
	int offloaded = -1;
	int offloaded_total = -1;
	float vram_used = -1.0;
	float vram_per_layer_avg = -1.0;
	std::map<std::string, int> tensor_type_map;
	std::map<std::string, std::string> meta_map;
}

int run_inference(int argc, char **argv, const std::function<bool(const nlohmann::json &metrics)> &onProgress,
				  const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus)
{
	onInferenceStatus = onStatus;
	onInferenceProgress = onProgress;
	update_inference_status(params.model_alias, wingman::WingmanItemStatus::preparing);

	llama_log_set(llama_log_callback_wingman, &llama);
	update_inference_status(params.model_alias, wingman::WingmanItemStatus::inferring);
	std::thread inferenceThread(metrics_reporting_thread, std::ref(llama));

	if (!svr.listen_after_bind()) {
		inferenceThread.join();
		return 1;
	}

	update_inference_status(params.model_alias, wingman::WingmanItemStatus::complete);
	keepRunning = true;

	std::thread inferenceThread(metrics_reporting_thread, std::ref(llama));
	update_inference_status(params.model_alias, wingman::WingmanItemStatus::complete);

	inferenceThread.join();

	inferenceThread.join();
}

void stop_inference()
{
	keepRunning = false;
	//uws_app->close();
	svr.stop();
	//us_timer_close(usAppMetricsTimer);
}

#ifndef WINGMAN_LIB
int main(int argc, char **argv)
{
	return run_inference(argc, argv, nullptr);
}
#endif
