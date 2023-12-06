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

namespace wingman::services {
	WingmanService::WingmanService(orm::ItemActionsFactory &factory
			, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress
			, const std::function<void(const std::string &alias, const WingmanItemStatus &status)> &onInferenceStatus
			, const std::function<bool(WingmanServiceAppItem *)> &onServiceStatus)
		: actions(factory)
		, onInferenceProgress(onInferenceProgress)
		, onInferenceStatus(onInferenceStatus)
		, onServiceStatus(onServiceStatus)
	{}

	void WingmanService::startInference(const WingmanItem &wingmanItem, bool overwrite) const
	{
		const auto modelPath = orm::DownloadItemActions::getDownloadItemOutputPath(wingmanItem.modelRepo, wingmanItem.filePath);
		//  "--port","6567",
		//	"--websocket-port","6568",
		//	"--ctx-size","0",
		//	"--n-gpu-layers","44",
		//	"--model","C:\\Users\\curtis.CARVERLAB\\.wingman\\models\\TheBloke[-]Xwin-LM-13B-V0.1-GGUF[=]xwin-lm-13b-v0.1.Q2_K.gguf",
		//	"--alias","TheBloke/Xwin-LM-13B-V0.1"

		std::map<std::string, std::string> options;

		// default options
		//options["--reverse-prompt"] = "USER:"; // `USER:` is specific to Vicuna models
		//options["--in-suffix"] = "Assistant:";

		options["--port"] = std::to_string(wingmanItem.port);
		options["--ctx-size"] = std::to_string(wingmanItem.contextSize);
		// TODO: if gpuLayers is -1, then try to determine automatically by loading the model, letting it crash if it loads too many layers, and then retrying with
		//   half as many layers until it loads successfully
		int gpuLayers = wingmanItem.gpuLayers;
		if (gpuLayers == -1) {
			gpuLayers = 99;
		}
		options["--n-gpu-layers"] = std::to_string(gpuLayers);
		options["--model"] = modelPath;
		options["--alias"] = wingmanItem.alias;

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

			ret = run_inference(static_cast<int>(cargs.size() - 1), cargs.data(), onInferenceProgress, onInferenceStatus);
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

	void WingmanService::updateServerStatus(const WingmanServiceAppItemStatus &status, std::optional<WingmanItem> wingmanItem, std::optional<std::string> error)
	{
		auto appItem = actions.app()->get(SERVER_NAME).value_or(AppItem::make(SERVER_NAME));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto wingmanServerItem = j.get<WingmanServiceAppItem>();
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
		WingmanServiceAppItem dsai;
		nlohmann::json j = dsai;
		AppItem item;
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

			std::string inferringAlias;

			std::thread stopInferenceThread([&]() {
				while (keepRunning) {
					if (inferringAlias.empty()) {
						std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
						continue;
					}
					if (auto i = actions.wingman()->get(inferringAlias)) {
						auto &item = i.value();
						if (item.status == WingmanItemStatus::cancelling) {
							spdlog::debug(SERVER_NAME + "::run Stopping inference of " + item.modelRepo + ": " + item.filePath + "...");
							stop_inference();
							item.status = WingmanItemStatus::cancelled;
							actions.wingman()->set(item);
							spdlog::debug(SERVER_NAME + "::run Stopped inference of " + item.modelRepo + ": " + item.filePath + ".");
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
				}
			});

			while (keepRunning) {
				updateServerStatus(WingmanServiceAppItemStatus::ready);
				spdlog::trace(SERVER_NAME + "::run Checking for queued wingmans...");
				if (auto nextItem = actions.wingman()->getNextQueued()) {
					auto &currentItem = nextItem.value();
					const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

					spdlog::info(SERVER_NAME + "::run Processing inference of " + modelName + "...");

					if (currentItem.status == WingmanItemStatus::queued) {
						updateServerStatus(WingmanServiceAppItemStatus::inferring, currentItem);

						spdlog::debug(SERVER_NAME + "::run calling startWingman " + modelName + "...");
						try {
							inferringAlias = currentItem.alias;
							startInference(currentItem, true);
							inferringAlias.clear();
						} catch (const std::exception &e) {
							spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
							currentItem.status = WingmanItemStatus::error;
							currentItem.error = e.what();
							actions.wingman()->set(currentItem);
							updateServerStatus(WingmanServiceAppItemStatus::error, currentItem, e.what());
						}
						spdlog::info(SERVER_NAME + "::run inference of " + modelName + " complete.");
						updateServerStatus(WingmanServiceAppItemStatus::ready);
						currentItem.status = WingmanItemStatus::complete;
						actions.wingman()->set(currentItem);
					}
				}

				spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
				std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
			}
			updateServerStatus(WingmanServiceAppItemStatus::stopping);
			stopInferenceThread.join();
			spdlog::debug(SERVER_NAME + "::run Wingman server stopped.");
		} catch (const std::exception &e) {
			spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
			stop();
		}
		updateServerStatus(WingmanServiceAppItemStatus::stopped);
	}

	void WingmanService::stop()
	{
		spdlog::debug(SERVER_NAME + "::stop Stopping wingman service...");
		keepRunning = false;
	}
} // namespace wingman::services
