#include <chrono>
#include <thread>
#include <filesystem>
#include <string>
#include <map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "owned_cstrings.h"
#include "wingman.service.h"
#include "wingman.inference.h"

WingmanService::WingmanService(wingman::ItemActionsFactory &actions_factory, int port, int gpuLayers
		, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress
		, const std::function<bool(wingman::WingmanServerAppItem *)> &onServiceStatus )
	: actions(actions_factory)
	, onInferenceProgress(onInferenceProgress)
	, onServiceStatus(onServiceStatus)
	, port(port)
	, gpuLayers(gpuLayers)
{}

void WingmanService::startInference(const wingman::WingmanItem &wingmanItem, bool overwrite)
{
	const auto modelPath = wingman::DownloadItemActions::getDownloadItemOutputPath(wingmanItem.modelRepo, wingmanItem.filePath);
	//  "--port","6567",
	//	"--websocket-port","6568",
	//	"--ctx-size","0",
	//	"--n-gpu-layers","44",
	//	"--model","C:\\Users\\curtis.CARVERLAB\\.wingman\\models\\TheBloke[-]Xwin-LM-13B-V0.1-GGUF[=]xwin-lm-13b-v0.1.Q2_K.gguf",
	//	"--alias","TheBloke/Xwin-LM-13B-V0.1"

	std::map<std::string, std::string> options;

	options["--port"] = std::to_string(port);
	options["--ctx-size"] = "0";
	// TODO: if gpuLayers is -1, then try to determine automatically by loading the model, letting it crash if it loads too many layers, and then retrying with
	//   half as many layers until it loads successfully
	if (gpuLayers == -1) {
		// TODO: set gpuLayers to a reasonable default (this only works on my machine)
		//gpuLayers = 99;
		//gpuLayers = 0;
		gpuLayers = 44;
	}
	options["--n-gpu-layers"] = std::to_string(gpuLayers);
	options["--model"] = modelPath;
	options["--alias"] = wingmanItem.modelRepo;

	// join pairs into a char** argv compatible array
	std::vector<std::string> args;
	int ret;
	do {
		args.clear();
		args.emplace_back("wingman");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			args.push_back(value);
		}
		owned_cstrings cargs(args);

		ret = run_inference(static_cast<int>(cargs.size() - 1), cargs.data(), onInferenceProgress);
		// return value of 100 means 'out of memory', so we need to try again with fewer layers
		if (ret == 100) {
			// try again using half the layers as before, until we're down to 1, then exit
			if (gpuLayers > 1) {
				gpuLayers /= 2;
				options["--n-gpu-layers"] = std::to_string(gpuLayers);
			} else {
				throw std::runtime_error("Out of memory.");
			}
		}
	} while (ret == 100);
}

void WingmanService::updateServerStatus(const wingman::WingmanServerAppItemStatus &status, std::optional<wingman::WingmanItem> wingmanItem, std::optional<std::string> error)
{
	auto appItem = actions.app()->get(SERVER_NAME).value_or(wingman::AppItem::make(SERVER_NAME));

	nlohmann::json j = nlohmann::json::parse(appItem.value);
	auto wingmanServerItem = j.get<wingman::WingmanServerAppItem>();
	wingmanServerItem.status = status;
	if (error) {
		wingmanServerItem.error = error;
	}
	if (onServiceStatus) {
		if (!onServiceStatus(&wingmanServerItem)) {
			spdlog::debug(SERVER_NAME + "::updateServerStatus onServiceStatus returned false, stopping server.");
			stop();
		}
	}
	nlohmann::json j2 = wingmanServerItem;
	appItem.value = j2.dump();
	actions.app()->set(appItem);
}

void WingmanService::initialize() const
{
	wingman::WingmanServerAppItem dsai;
	nlohmann::json j = dsai;
	wingman::AppItem item;
	item.name = SERVER_NAME;
	item.value = j.dump();
	actions.app()->set(item);

	actions.wingman()->reset();
}

void WingmanService::run()
{
	try {
		if (!keepRunning) {
			return;
		}

		spdlog::debug(SERVER_NAME + "::run Wingman service started.");

		initialize();

		while (keepRunning) {
			updateServerStatus(wingman::WingmanServerAppItemStatus::ready);
			spdlog::trace(SERVER_NAME + "::run Checking for queued wingmans...");
			if (auto nextItem = actions.wingman()->getNextQueued()) {
				auto &currentItem = nextItem.value();
				const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

				spdlog::info(SERVER_NAME + "::run Processing inference of " + modelName + "...");

				if (currentItem.status == wingman::WingmanItemStatus::queued) {
					// Update status to inferring
					currentItem.status = wingman::WingmanItemStatus::inferring;
					actions.wingman()->set(currentItem);
					updateServerStatus(wingman::WingmanServerAppItemStatus::inferring, currentItem);

					spdlog::debug(SERVER_NAME + "::run calling startWingman " + modelName + "...");
					try {
						startInference(currentItem, true);
					} catch (const std::exception &e) {
						spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
						currentItem.status = wingman::WingmanItemStatus::error;
						currentItem.error = e.what();
						actions.wingman()->set(currentItem);
						updateServerStatus(wingman::WingmanServerAppItemStatus::error, currentItem, e.what());
					}
					spdlog::info(SERVER_NAME + "::run Wingman of " + modelName + " complete.");
					updateServerStatus(wingman::WingmanServerAppItemStatus::ready);
					currentItem.status = wingman::WingmanItemStatus::complete;
					actions.wingman()->set(currentItem);
				}
			}

			spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
			std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
		}
		updateServerStatus(wingman::WingmanServerAppItemStatus::stopping);
		spdlog::debug(SERVER_NAME + "::run Wingman server stopped.");
	} catch (const std::exception &e) {
		spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
		stop();
	}
	updateServerStatus(wingman::WingmanServerAppItemStatus::stopped);
}

void WingmanService::stop()
{
	keepRunning = false;
}

int WingmanService::getPort() const
{
	return port;
}

int WingmanService::getGpuLayers() const
{
	return gpuLayers;
}
