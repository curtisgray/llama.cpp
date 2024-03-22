#pragma once
#include <functional>

#include "httplib.h"
#include "types.h"

#include "orm.h"
#include "util.hpp"

void stop_inference();
int run_inference(int argc, char **argv,
				  std::function<void()>& shutdownInference,
				  const std::function<bool(const nlohmann::json &metrics)> &onProgress,
                  const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus,
                  const std::function<void(const wingman::WingmanServiceAppItemStatus &status, std::optional<std::string> error)> &onServiceStatus);
inline std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
inline std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> onInferenceStatus = nullptr;
inline std::function<void(const wingman::WingmanServiceAppItemStatus &status, std::optional<std::string> error)> onInferenceServiceStatus = nullptr;
void update_inference_status(const std::string &alias, const wingman::WingmanItemStatus &status);
void update_inference_service_status(const wingman::WingmanServiceAppItemStatus& status, std::optional<std::string> error = std::nullopt);
void metrics_reporting_thread(const std::function<nlohmann::json()> &callback);

struct extra_server_info {
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
	bool has_next_token = false;
};

inline std::unique_ptr<httplib::Server> svr;
inline bool keepRunning = true;
inline wingman::WingmanItemStatus lastStatus = wingman::WingmanItemStatus::unknown;
inline std::string currentInferringAlias;
