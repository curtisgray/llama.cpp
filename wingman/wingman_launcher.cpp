
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>
#include <process.hpp>

#include "orm.h"
#include "curl.h"

namespace wingman {
	using namespace std::chrono_literals;

	const std::string SERVER_NAME = "Wingman_Launcher";

	std::filesystem::path logs_dir;

	std::function<void(int)> shutdown_handler;
	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	orm::ItemActionsFactory actions_factory;

	bool requested_shutdown;

	int Start(const int port, const int websocketPort, const int gpuLayers, const bool isCudaOutOfMemoryRestart)
	{
		spdlog::set_level(spdlog::level::debug);

		logs_dir = actions_factory.getLogsDir();

		TinyProcessLib::Process serverProcess(
			std::vector<std::string>
		{
			"wingman",
				"--port",
				std::to_string(port),
				"--websocket-port",
				std::to_string(websocketPort),
				"--gpu-layers",
				std::to_string(gpuLayers),
		},
			"",
			[](const char *bytes, size_t n) {
			std::cout << "Wingman: " << std::string(bytes, n);
		});

	// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			spdlog::debug(" (wingman_launcher-start) SIGINT received.");
			// if we have received the signal before, abort.
			if (requested_shutdown) abort();
			// First SIGINT recieved, attempt a clean shutdown
			requested_shutdown = true;
			serverProcess.kill();
		};
		if (const auto res = std::signal(SIGINT, SIGINT_Callback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			throw std::runtime_error("Failed to register signal handler.");
		}
		return serverProcess.get_exit_status();

	}
} // namespace wingman

struct Params {
	int port = 6567;
	int websocketPort = 6568;
	int gpuLayers = -1;
};

static void ParseParams(int argc, char **argv, Params &params)
{
	std::string arg;
	bool invalid_param = false;

	for (int i = 1; i < argc; i++) {
		arg = argv[i];
		if (arg == "--port") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.port = std::stoi(argv[i]);
		} else if (arg == "--gpu-layers" || arg == "-ngl" || arg == "--n-gpu-layers") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.gpuLayers = std::stoi(argv[i]);
		} else if (arg == "--websocket-port") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.websocketPort = std::stoi(argv[i]);
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}

	if (invalid_param) {
		throw std::runtime_error("invalid parameter for argument: " + arg);
	}
}

int main(const int argc, char **argv)
{
	spdlog::set_level(spdlog::level::debug);
	auto params = Params();

	ParseParams(argc, argv, params);

	try {
		wingman::orm::ItemActionsFactory actionsFactory;
		spdlog::info("Starting Wingman Launcher...");
		while (!wingman::requested_shutdown) {
			spdlog::debug("Starting Wingman with port: {}, websocket port: {}, gpu layers: {}", params.port, params.websocketPort, params.gpuLayers);
			const int result = wingman::Start(params.port, params.websocketPort, params.gpuLayers, false);
			if (wingman::requested_shutdown) {
				spdlog::debug("Wingman exited with return value: {}. Shutdown requested...", result);
				break;
			}
			if (result != 0) {
				spdlog::error("Wingman exited with return value: {}", result);
				// when the app exits, we need to check if it was due to an out of memory error
				//  since there's currently no way to detect this from the app itself, we need to
				//  check the WingmanService status in the database to see if inference was running
				//  when the app exited. If so, we need to check the WingmanItem status to see if it
				//  for an item that was `inferring`. If so, we will try the last item with a status
				//  of `complete`, and set that item to `inferring` and restart the app.

				auto appItem = actionsFactory.app()->get("WingmanService");
				if (appItem) {
					bool isInferring = false;
					nlohmann::json j = nlohmann::json::parse(appItem.value().value);
					auto wingmanServerItem = j.get<wingman::WingmanServiceAppItem>();
					spdlog::debug("WingmanServiceAppItem status at last exit: {}", wingman::WingmanServiceAppItem::toString(wingmanServerItem.status));
					switch (wingmanServerItem.status) {
						case wingman::WingmanServiceAppItemStatus::ready:
							// service was initialized and awaiting a request
							break;
						case wingman::WingmanServiceAppItemStatus::starting:
							// service was initializing
							break;
						case wingman::WingmanServiceAppItemStatus::preparing:
							// service is loading and preparing the model (this is usually where an out of memory error would occur)
						case wingman::WingmanServiceAppItemStatus::inferring:
							// service is inferring (an out of memory error could occur here, but it's less likely)
							isInferring = true;
							break;
						case wingman::WingmanServiceAppItemStatus::stopping:
							break;
						case wingman::WingmanServiceAppItemStatus::stopped:
							break;
						case wingman::WingmanServiceAppItemStatus::error:
							break;
						case wingman::WingmanServiceAppItemStatus::unknown:
							break;
					}
					if (isInferring) {
						spdlog::debug("WingmanServiceAppItem status was inferring, checking WingmanItem status...");
						// get the last active item and set it to `error`
						auto activeItems = actionsFactory.wingman()->getAllActive();
						if (!activeItems.empty()) {
							spdlog::debug("Found {} items with active status", activeItems.size());
							std::sort(activeItems.begin(), activeItems.end(), [](const wingman::WingmanItem &a, const wingman::WingmanItem &b) {
								return a.updated < b.updated;
							});
							auto lastInferring = activeItems[activeItems.size() - 1];
							lastInferring.status = wingman::WingmanItemStatus::error;
							lastInferring.error = "Exited during inference. Likely out of GPU memory.";
							actionsFactory.wingman()->set(lastInferring);
							spdlog::debug("Set item {} to error", lastInferring.alias);
						}
						spdlog::debug("Checking for last complete item...");
						// get the last item that was `complete`
						auto complete = actionsFactory.wingman()->getByStatus(wingman::WingmanItemStatus::complete);
						if (!complete.empty()) {
							spdlog::debug("Found {} items with status complete", complete.size());
							std::sort(complete.begin(), complete.end(), [](const wingman::WingmanItem &a, const wingman::WingmanItem &b) {
								return a.updated < b.updated;
							});
							auto lastComplete = complete[complete.size() - 1];
							lastComplete.status = wingman::WingmanItemStatus::inferring;
							actionsFactory.wingman()->set(lastComplete);
							spdlog::debug("Set item {} to inferring", lastComplete.alias);
						} else {
							spdlog::debug("No items with status complete found");
						}
					}
				} else {
					spdlog::debug("WingmanServiceAppItem not found");
				}
			}
		}
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("Wingman Launcher exited.");
	return 0;
}
