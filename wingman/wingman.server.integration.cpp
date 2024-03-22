
#include <string>
#include <thread>

#include "json.hpp"
#include "wingman.server.integration.h"

#include "orm.h"
#include "types.h"

using namespace nlohmann;

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

void metrics_reporting_thread(const std::function<json()> &callback)
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
