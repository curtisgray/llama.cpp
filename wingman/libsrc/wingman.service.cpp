#include <chrono>
#include <thread>
#include <filesystem>
#include <string>
#include <map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "owned_cstrings.h"
#include "wingman.service.h"
#include "wingman.server.integration.h"

namespace wingman::services {
	WingmanService::WingmanService(orm::ItemActionsFactory &factory
			, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress
			, const std::function<void(const std::string &alias, const WingmanItemStatus &status)> &onInferenceStatus
	)
		: actions(factory)
		, onInferenceProgress(onInferenceProgress)
		, onInferenceStatus(onInferenceStatus)
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
			} else if (ret == 1) {
				// there was an error during loading, binding to the port, or listening for connections
				throw std::runtime_error("Wingman exited with error code 1. There was an error during loading, binding to the port, or listening for connections");
			} else if (ret != 0) {
				// there was an error during inference
				throw std::runtime_error("Wingman exited with error code " + std::to_string(ret));
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

			std::thread stopInferenceThread([&]() {
				while (keepRunning) {
					auto cancellingItems = actions.wingman()->getByStatus(WingmanItemStatus::cancelling);
					for (auto item : cancellingItems) {
						spdlog::debug(SERVER_NAME + "::run Stopping inference of " + item.modelRepo + ": " + item.filePath + "...");
						stop_inference();
						item.status = WingmanItemStatus::complete;
						actions.wingman()->set(item);
						// after inference has stopped, we need to wait a bit before we can start another inference
						spdlog::trace(SERVER_NAME + "::run Waiting 2 seconds...");
						std::this_thread::sleep_for(std::chrono::milliseconds(2000));
						spdlog::debug(SERVER_NAME + "::run Stopped inference of " + item.modelRepo + ": " + item.filePath + ".");
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
				}
			});

			updateServerStatus(WingmanServiceAppItemStatus::ready);
			while (keepRunning) {
				spdlog::trace(SERVER_NAME + "::run Checking for queued wingmen...");
				if (auto nextItem = actions.wingman()->getNextQueued()) {
					auto &currentItem = nextItem.value();
					const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

					// if the model file doesn't exist, then we need to remove it from the db and continue
					const auto dm = actions.download()->get(currentItem.modelRepo, currentItem.filePath);
					if (!dm) {
						spdlog::warn(SERVER_NAME + "::run Model file does not exist: " + modelName);
						currentItem.status = WingmanItemStatus::error;
						currentItem.error = "Model file does not exist: " + modelName;
						actions.wingman()->set(currentItem);
						continue;
					}
					spdlog::info(SERVER_NAME + "::run Processing inference of " + modelName + "...");

					updateServerStatus(WingmanServiceAppItemStatus::inferring, currentItem);

					spdlog::debug(SERVER_NAME + "::run calling startWingman " + modelName + "...");
					try {
						hasInferred = true;
						startInference(currentItem, true);
					}
					catch (const wingman::CudaOutOfMemory &e) {
						// throw this exception so that we can retry with fewer layers
						spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
						//throw;
						// set the wingman item to error so that it doesn't get stuck in the queue
						currentItem.status = WingmanItemStatus::error;
						currentItem.error = e.what();
						actions.wingman()->set(currentItem);
						// When a CUDA out of memory error occurs, it's most likely bc the loaded model was too big
						//  for the GPU's memory. We need to remove the model from the db and either select a default
						//  model, or pick the last completed model. For now we'll go wiht picking the last completed
						auto wingmanItems = actions.wingman()->getAll();
						if (!wingmanItems.empty()) {
							auto lastCompletedItem = std::find_if(wingmanItems.rbegin(), wingmanItems.rend(), [](const auto &item) {
								return item.status == WingmanItemStatus::complete;
							});
							if (lastCompletedItem != wingmanItems.rend()) {
								spdlog::info(" (start) Restarting inference with last completed model: {}", lastCompletedItem->modelRepo);
								lastCompletedItem->status = WingmanItemStatus::queued;
								actions.wingman()->set(*lastCompletedItem);
							} else {
								// for now we'll do nothing and let the user select a model
							}
						}
					}
					catch (const std::exception &e) {
						spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
						currentItem.status = WingmanItemStatus::error;
						currentItem.error = e.what();
						actions.wingman()->set(currentItem);
						updateServerStatus(WingmanServiceAppItemStatus::error, currentItem, e.what());
					}
					spdlog::info(SERVER_NAME + "::run inference of " + modelName + " complete.");
					updateServerStatus(WingmanServiceAppItemStatus::ready);
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
