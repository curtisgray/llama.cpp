
#include <csignal>
#include <iostream>

#include "orm.h"
#include "download.service.h"
#include "spdlog/spdlog.h"

std::atomic requested_shutdown = false;

std::function<void(int)> shutdown_callback_handler;
void SignalCallback(int signal)
{
	shutdown_callback_handler(signal);
}

bool OnDownloadProgress(const wingman::curl::Response * response)
{
	std::cerr << fmt::format(
		std::locale("en_US.UTF-8"),
		"{}: {} of {} ({:.1f})\t\t\t\t\r",
		response->file.item->modelRepo,
		wingman::util::prettyBytes(response->file.totalBytesWritten),
		wingman::util::prettyBytes(response->file.item->totalBytes),
		response->file.item->progress);
	return true;
}
std::function<void(wingman::curl::Response *)> on_download_progress_handler = OnDownloadProgress;

void Start()
{
	spdlog::set_level(spdlog::level::debug);

	wingman::orm::ItemActionsFactory actionsFactory;

	spdlog::info("Starting servers...");

	auto handler = [&](const wingman::curl::Response *response) {
		std::cerr << fmt::format(
			std::locale("en_US.UTF-8"),
			"{}: {} of {} ({:.1f})\t\t\t\t\r",
			response->file.item->modelRepo,
			wingman::util::prettyBytes(response->file.totalBytesWritten),
			wingman::util::prettyBytes(response->file.item->totalBytes),
			response->file.item->progress);
	};

	// NOTE: all of these signatures work for passing the handler to the DownloadService constructor
	//DownloadService server(actionsFactory, handler);
	wingman::services::DownloadService server(actionsFactory, OnDownloadProgress);
	//DownloadService server(actionsFactory, onDownloadProgressHandler);
	std::thread serverThread(&wingman::services::DownloadService::run, &server);

	// wait for ctrl-c
	shutdown_callback_handler = [&](int /* signum */) {
		spdlog::debug(" (start) SIGINT received.");
		// if we have received the signal before, abort.
		if (requested_shutdown) abort();
		// First SIGINT recieved, attempt a clean shutdown
		requested_shutdown = true;
		server.stop();
	};

	if (const auto res = std::signal(SIGINT, SignalCallback); res == SIG_ERR) {
		spdlog::error(" (start) Failed to register signal handler.");
		return;
	}

	std::cout << "Press Ctrl-C to quit" << std::endl;
	serverThread.join();
	spdlog::info("Servers stopped.");
}

int main()
{
	try {
		Start();
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	return 0;
}
