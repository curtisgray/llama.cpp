
#include <string>
#include <thread>

#include "types.h"
#include "wingman.server.integration.h"

#include "orm.h"

using namespace nlohmann;

void update_inference_status(const std::string &alias, const wingman::WingmanItemStatus &status)
{
	lastStatus = status;
	if (onInferenceStatus != nullptr) {
		onInferenceStatus(alias, status);
	}
}

void metrics_reporting_thread(const std::function<json()> &callback)
{
	spdlog::debug("metrics_reporting_thread started...");
	while (keepRunning) {
		std::chrono::milliseconds update_interval(1000);
		if (onInferenceProgress != nullptr) {
			const auto kr = onInferenceProgress(callback());
			if (!kr)
				return;
		}
		std::this_thread::sleep_for(update_interval);
	}
	spdlog::debug("metrics_reporting_thread exiting.");
}

void stop_inference()
{
	spdlog::debug("stop_inference called");
	if (keepRunning) {
		keepRunning = false;
		lastStatus = wingman::WingmanItemStatus::unknown;
		lastAlias = "";
		// if svr is running, stop it
		if (svr.is_running()) {
			spdlog::debug("stop_inference stopping svr");
			svr.stop();
		}
	} else {
		spdlog::debug("stop_inference already stopped");
	}
}
