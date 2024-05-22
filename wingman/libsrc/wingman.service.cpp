#include <chrono>
#include <thread>
#include <filesystem>
#include <string>
#include <map>

// #include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "json.hpp"
#include "owned_cstrings.h"
#include "wingman.service.h"

#include "exceptions.h"
#include "wingman.server.integration.h"

namespace wingman::services {
	WingmanService::WingmanService(orm::ItemActionsFactory &factory
			, std::function<void()> &requestShutdownInference
			, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress
			, const std::function<void(const std::string &alias, const WingmanItemStatus &status)> &onInferenceStatus
			, const std::function<void(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)> &onInferenceServiceStatus
	)
		: actions(factory)
		, onInferenceProgress(onInferenceProgress)
		, onInferenceStatus(onInferenceStatus)
		, onInferenceServiceStatus(onInferenceServiceStatus)
		, requestShutdownInference(requestShutdownInference)
	{}

	void WingmanService::ShutdownInference()
	{
		if (requestShutdownInference != nullptr) {
			requestShutdownInference();
		}
	}

	void WingmanService::startInference(const WingmanItem &wingmanItem, bool overwrite)
	{
		const auto modelPath = orm::DownloadItemActions::getDownloadItemOutputPath(wingmanItem.modelRepo, wingmanItem.filePath);
		//  "--port","6567",
		//	"--websocket-port","6568",
		//	"--ctx-size","0",
		//	"--n-gpu-layers","44",
		//	"--model","C:\\Users\\curtis.CARVERLAB\\.wingman\\models\\TheBloke[-]Xwin-LM-13B-V0.1-GGUF[=]xwin-lm-13b-v0.1.Q2_K.gguf",
		//	"--alias","TheBloke/Xwin-LM-13B-V0.1"
		//  "--chat-template", "chatml"

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
		options["--chat-template"] = "chatml";
		options["--embedding"] = "";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		int ret;
		do {
			args.clear();
			args.emplace_back("wingman");
			for (const auto &[option, value] : options) {
				args.push_back(option);
				if (!value.empty()) {
					args.push_back(value);
				}
			}
			owned_cstrings cargs(args);
			try {
				isInferring = true;
				ret = run_inference(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference, onInferenceProgress, onInferenceStatus, onInferenceServiceStatus);
				isInferring = false;
			} catch (const std::exception &e) {
				spdlog::error(SERVER_NAME + "::startInference Exception (run_inference): " + std::string(e.what()));
				// throw;
				isInferring = false;
			}
			// set the requestShutdownInference to nullptr so that we don't call it again
			requestShutdownInference = nullptr;
			stop_inference();
			// return value of 100 means 'out of memory', so we need to try again with fewer layers
			// spdlog::info("{}::startInference run_inference returned {}.", SERVER_NAME, ret);
			// IMPORTANT: the following message is used by the frontend to determine if the model is out of memory
			std::cerr << fmt::format("{}::startInference run_inference returned {}.\n", SERVER_NAME, ret);
			if (ret == 100) {
				// try again using half the layers as before, until we're down to 1, then exit
				if (gpuLayers > 1) {
					gpuLayers /= 2;
					options["--n-gpu-layers"] = std::to_string(gpuLayers);
				} else {
					throw std::runtime_error("Out of memory.");
				}
			} else if (ret == 1024) {
				throw ModelLoadingException();
			} else if (ret == 1) {
				// there was an error during loading, binding to the port, or listening for connections
				throw std::runtime_error("Wingman exited with error code 1. There was an error during loading, binding to the port, or listening for connections");
			} else if (ret != 0) {
				// there was an error during inference
				throw std::runtime_error("Wingman exited with error code " + std::to_string(ret));
			}
		} while (ret == 100);
	}

	void WingmanService::updateServiceStatus(const WingmanServiceAppItemStatus& status, std::optional<std::string> error)
	{
		// auto appItem = actions.app()->get(SERVER_NAME).value_or(AppItem::make(SERVER_NAME));
		//
		// nlohmann::json j = nlohmann::json::parse(appItem.value);
		// auto wingmanServerItem = j.get<WingmanServiceAppItem>();
		// wingmanServerItem.status = status;
		// if (error) {
		// 	wingmanServerItem.error = error;
		// }
		// nlohmann::json j2 = wingmanServerItem;
		// appItem.value = j2.dump();
		// actions.app()->set(appItem);
		if (onInferenceServiceStatus != nullptr)
			onInferenceServiceStatus(status, error);
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
			updateServiceStatus(WingmanServiceAppItemStatus::starting);

			spdlog::debug(SERVER_NAME + "::run Wingman service started.");

			initialize();

			std::thread stopInferenceThread([&]() {
				while (keepRunning) {
					auto cancellingItems = actions.wingman()->getByStatus(WingmanItemStatus::cancelling);
					for (auto item : cancellingItems) {
						spdlog::debug(SERVER_NAME + "::run Stopping inference of " + item.modelRepo + ": " + item.filePath + "...");
						ShutdownInference();
						item.status = WingmanItemStatus::complete;
						actions.wingman()->set(item);
						// wait for isInferring to be false
						spdlog::trace(SERVER_NAME + "::run Waiting for inference to complete...");
						auto stopInferenceInitiatedTime = std::chrono::steady_clock::time_point::min();
						while (isInferring) {
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
						}
						auto now = std::chrono::steady_clock::now();
						auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stopInferenceInitiatedTime).count();
							// std::this_thread::sleep_for(std::chrono::milliseconds(2000));
						spdlog::debug("{}::run Inference of {}:{} stopped after {}ms", SERVER_NAME, item.modelRepo, item.filePath, elapsed);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
				}
			});

			updateServiceStatus(WingmanServiceAppItemStatus::ready);
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

					updateServiceStatus(WingmanServiceAppItemStatus::preparing);

					spdlog::debug(SERVER_NAME + "::run calling startWingman " + modelName + "...");
					try {
						hasInferred = true;
						startInference(currentItem, true);
					}
					catch (ModelLoadingException &e) {
						spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
						// if there was an error loading the model, then we need to remove it from the db and exit
						currentItem.status = WingmanItemStatus::error;
						currentItem.error = e.what();
						actions.wingman()->set(currentItem);
						updateServiceStatus(WingmanServiceAppItemStatus::error, e.what());
						// the app is in an error state, so we need to exit
						stop();
						return;
					}
					catch (const std::exception &e) {
						spdlog::error(SERVER_NAME + "::run Exception (startWingman): " + std::string(e.what()));
						if (std::string(e.what()) == "Wingman exited with error code 1024. There was an error loading the model.") {
							currentItem.status = WingmanItemStatus::error;
							currentItem.error = "There is not enough memory available to load the AI model.";
							actions.wingman()->set(currentItem);
							updateServiceStatus(WingmanServiceAppItemStatus::error, e.what());
						}
					}
					spdlog::info(SERVER_NAME + "::run inference of " + modelName + " complete.");
					updateServiceStatus(WingmanServiceAppItemStatus::ready);
				}

				spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
				std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
			}
			updateServiceStatus(WingmanServiceAppItemStatus::stopping);
			stopInferenceThread.join();
			spdlog::debug(SERVER_NAME + "::run Wingman server stopped.");
		} catch (const std::exception &e) {
			spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
			stop();
		}
		updateServiceStatus(WingmanServiceAppItemStatus::stopped);
	}

	void WingmanService::stop()
	{
		spdlog::debug(SERVER_NAME + "::stop Stopping wingman service...");
		keepRunning = false;
		ShutdownInference();
	}
} // namespace wingman::services
