
#include <string>
#include <thread>

#include "json.hpp"
#include "wingman.server.integration.h"

#include "orm.h"
#include "types.h"
#include "spdlog/spdlog.h"

void wingman_server_log(const char *level, const char *function, int line, const char *message, const nlohmann::ordered_json &extra)
{
	if (disableInferenceLogging)
		return;

	std::stringstream ss_tid;
	ss_tid << std::this_thread::get_id();
	auto log = nlohmann::ordered_json {
		{"tid",       ss_tid.str()},
		{"timestamp", time(nullptr)},
	};

	log.merge_patch({
		// {"level",    level},
		{"function", function},
		{"line",     line},
		{"msg",      message},
	});

	if (!extra.empty()) {
		log.merge_patch(extra);
	}

	// printf("%s\n", log.dump(-1, ' ', false, json::error_handler_t::replace).c_str());
	const std::string logStr = log.dump();

	if (strcmp(level, "INFO") == 0) {
		spdlog::info(logStr);
	} else if (strcmp(level, "WARN") == 0) {
		spdlog::warn(logStr);
	} else if (strcmp(level, "ERR") == 0) {
		spdlog::error(logStr);
	} else if (strcmp(level, "VERB") == 0) {
		spdlog::debug(logStr);
	} else {
		spdlog::trace(logStr); // Default to trace if the level is not recognized
	}
}

void request_emergency_shutdown()
{
	keepRunning = false;
}

void update_inference_status(const std::string &alias, const wingman::WingmanItemStatus &status)
{
	lastStatus = status;
	if (onInferenceStatus != nullptr) {
		onInferenceStatus(alias, status);
	}
}

void update_inference_service_status(const wingman::WingmanServiceAppItemStatus& status, std::optional<std::string> error)
{
	if (onInferenceServiceStatus != nullptr) {
		onInferenceServiceStatus(status, error);
	}
}

void metrics_reporting_thread(const std::function<nlohmann::json()> &callback)
{
	spdlog::debug("metrics_reporting_thread started...");
	while (keepRunning) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (onInferenceProgress != nullptr) {
			const auto kr = onInferenceProgress(callback());
			if (!kr)
				return;
		}
	}
	spdlog::debug("metrics_reporting_thread exiting.");
}

void stop_inference()
{
	spdlog::debug("stop_inference called");
	if (keepRunning) {
		keepRunning = false;
		lastStatus = wingman::WingmanItemStatus::unknown;
		currentInferringAlias = "";
		if (svr->is_running()) {
			spdlog::debug("stop_inference stopping svr");
			svr->stop();
		}
	} else {
		currentInferringAlias = "";	// always reset currentInferringAlias
		spdlog::debug("stop_inference already stopped");
	}
}
