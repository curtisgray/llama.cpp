
#include <csignal>
#include <iostream>

#include "orm.h"
#include "download.service.h"
#include "wingman.inference.h"
#include "wingman.service.h"

std::atomic requestedShutdown = false;

std::function<void(int)> shutdown_handler;
void signal_handler(int signal)
{
	shutdown_handler(signal);
}

bool onDownloadProgress(const wingman::curl::Response *response)
{
	std::cerr << fmt::format(
		std::locale("en_US.UTF-8"),
		"{}: {} of {} ({:.1f})\t\t\t\t\r",
		response->file.item->modelRepo,
		wingman::util::prettyBytes(response->file.totalBytesWritten),
		wingman::util::prettyBytes(response->file.item->totalBytes),
		response->file.item->progress);
	return !requestedShutdown;
}
//std::function<void(wingman::curl::Response *)> onDownloadProgressHandler = onDownloadProgress;

void onInferenceProgress(const wingman::WingmanItem *item)
{
	std::cerr << ".";
}

struct Params {
	int port = 6567;
	int websocket_port = 6568;
	int gpu_layers = -1;
};

static void parseParams(int argc, char **argv, Params &params)
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
			params.gpu_layers = std::stoi(argv[i]);
		} else if (arg == "--websocket-port") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.websocket_port = std::stoi(argv[i]);
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}

	if (invalid_param) {
		throw std::runtime_error("invalid parameter for argument: " + arg);
	}
}

void start(int port, int websocketPort, int gpuLayers)
{
	spdlog::set_level(spdlog::level::debug);

	wingman::ItemActionsFactory actionsFactory;

	spdlog::info("Starting servers...");

	//auto handler = [&](const wingman::curl::Response *response) {
	//	std::cerr << fmt::format(
	//		std::locale("en_US.UTF-8"),
	//		"{}: {} of {} ({:.1f})\t\t\t\t\r",
	//		response->file.item->modelRepo,
	//		wingman::util::prettyBytes(response->file.totalBytesWritten),
	//		wingman::util::prettyBytes(response->file.item->totalBytes),
	//		response->file.item->progress);
	//};

	// NOTE: all of these signatures work for passing the handler to the DownloadService constructor
	//DownloadService downloadService(actionsFactory, handler);
	DownloadService downloadService(actionsFactory, onDownloadProgress);
	//DownloadService downloadService(actionsFactory, onDownloadProgressHandler);
	std::thread downloadServiceThread(&DownloadService::run, &downloadService);

	WingmanService wingmanService(actionsFactory, port, websocketPort, gpuLayers);
	std::thread wingmanServiceThread(&WingmanService::run, &wingmanService);

	// wait for ctrl-c
	shutdown_handler = [&](int /* signum */) {
		spdlog::debug(" (start) SIGINT received.");
		// if we have received the signal before, abort.
		if (requestedShutdown) abort();
		// First SIGINT recieved, attempt a clean shutdown
		requestedShutdown = true;
		downloadService.stop();
		wingmanService.stop();
		stop_inference();
	};

	if (const auto res = std::signal(SIGINT, signal_handler); res == SIG_ERR) {
		spdlog::error(" (start) Failed to register signal handler.");
		return;
	}

	std::cout << "Press Ctrl-C to quit" << std::endl;
	downloadServiceThread.join();
	wingmanServiceThread.join();
	spdlog::info("Servers stopped.");
}

int main(const int argc, char **argv)
{
	auto params = Params();

	parseParams(argc, argv, params);

	try {
		start(params.port, params.websocket_port, params.gpu_layers);
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	return 0;
}
