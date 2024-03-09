
#include <csignal>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>
// #include <process.hpp>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>

#include "orm.h"
#include "curl.h"

namespace wingman {
	using namespace std::chrono_literals;
	namespace bp = boost::process;
	namespace fs = std::filesystem;

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
		logs_dir = actions_factory.getLogsDir();
		spdlog::debug("Logs directory: {}", logs_dir.string());
		spdlog::info("Starting Wingman server...");
		// Enhanced logging to verify current working directory.
		spdlog::info("Current Working Directory: {}", fs::current_path().string());

		// Assuming 'wingman' executable is in the same directory as the current process
		fs::path executablePath = fs::current_path();
#ifdef _WIN32
		executablePath /= "wingman.exe";
#else
		executablePath /= "wingman";
#endif
		// Verifying if the path to 'wingman' executable exists
		if (!fs::exists(executablePath)) {
			spdlog::error("Executable path does not exist: {}", executablePath.string());
			throw std::runtime_error("Executable path does not exist.");
		}

		std::string command = executablePath.string(); // Converts the path to a string suitable for Boost.Process

		std::vector<std::string> args = {
			"--port", std::to_string(port),
			"--websocket-port", std::to_string(websocketPort),
			"--gpu-layers", std::to_string(gpuLayers)
		};

		// Setup Boost.Process io_context for asynchronous operation
		boost::asio::io_context io_context;

		// Setup asynchronous stdout and stderr streams
		bp::async_pipe out_pipe(io_context), err_pipe(io_context);

		// Declare streambufs outside the lambda functions to ensure their lifespan
		boost::asio::streambuf out_buf, err_buf;

		// Launch the process
		bp::child serverProcess(
			command, bp::args(args),
			bp::std_out > out_pipe,
			bp::std_err > err_pipe,
			io_context
		);

		std::function<void(boost::system::error_code, std::size_t)> stdout_handler, stderr_handler;

		auto read_stdout = [&]() {
			std::istream stream(&out_buf);
			std::string line;
			std::getline(stream, line);
			spdlog::debug("Wingman: {}", line);

			// Continue reading if the process is still running and the stream is not at EOF
			if (serverProcess.running() && stream.good()) {
				boost::asio::async_read_until(out_pipe, out_buf, '\n', stdout_handler);
			}
		};

		auto read_stderr = [&]() {
			std::istream stream(&err_buf);
			std::string line;
			std::getline(stream, line);
			spdlog::error("Wingman Error: {}", line);

			// Continue reading if the process is still running and the stream is not at EOF
			if (serverProcess.running() && stream.good()) {
				boost::asio::async_read_until(err_pipe, err_buf, '\n', stderr_handler);
			}
		};

		stdout_handler = [&](boost::system::error_code ec, std::size_t size) {
			if (!ec) read_stdout();
		};

		stderr_handler = [&](boost::system::error_code ec, std::size_t size) {
			if (!ec) read_stderr();
		};

		// Start reading output
		boost::asio::async_read_until(out_pipe, out_buf, '\n', stdout_handler);
		boost::asio::async_read_until(err_pipe, err_buf, '\n', stderr_handler);

		shutdown_handler = [&](int /* signum */) {
			spdlog::debug("SIGINT received. Attempting to shutdown Wingman server gracefully...");

			if (requested_shutdown) abort(); // Prevent double SIGINT causing immediate abort

			requested_shutdown = true;

			boost::asio::steady_timer timer(io_context);
			bool timeout_expired = false;

			timer.expires_after(std::chrono::seconds(20));
			timer.async_wait([&](const boost::system::error_code & /*e*/) {
				if (serverProcess.running()) {
					spdlog::warn("Timeout expired. Forcibly terminating the Wingman server process.");
					serverProcess.terminate();
					timeout_expired = true;
				}
			});

			// // Construct the shutdown URL
			// const std::string shutdownUrl = "http://localhost:" + std::to_string(websocketPort) + "/api/shutdown";
			//
			// try {
			// 	// Make the HTTP GET call to initiate server shutdown
			// 	auto response = curl::Fetch(shutdownUrl); // Using the Fetch function from curl.h
			// 	if (response.curlCode == CURLE_OK && response.statusCode == 200) {
			// 		spdlog::debug("Shutdown request sent successfully. Awaiting server shutdown...");
			// 	} else {
			// 		spdlog::error("Failed to initiate server shutdown. HTTP Status: {}, CURL Code: {}", response.statusCode, static_cast<long>(response.curlCode));
			// 	}
			// } catch (const std::exception &e) {
			// 	spdlog::error("Exception during shutdown sequence: {}", e.what());
			// }
			//
			// // Wait for the server process to exit with a timeout
			// boost::asio::steady_timer timer(io_context);
			// bool timeout_expired = false;
			//
			// timer.expires_after(std::chrono::seconds(20));
			// timer.async_wait([&](const boost::system::error_code & /*e*/) {
			// 	if (serverProcess.running()) {
			// 		spdlog::warn("Timeout expired. Forcibly terminating the Wingman server process.");
			// 		serverProcess.terminate();
			// 		timeout_expired = true;
			// 	}
			// });

			while (serverProcess.running() && !timeout_expired) {
				io_context.run_one();
			}

			if (!timeout_expired) {
				timer.cancel(); // Cancel the timer if the process exits before the timeout
			}

			spdlog::debug("Wingman server process exited.");
		};

		// Setup signal handler for SIGINT to gracefully terminate the process
		// shutdown_handler = [&](int /* signum */) {
		// 	spdlog::debug("SIGINT received. Attempting to stop Wingman server...");
		// 	if (requested_shutdown) abort();
		// 	requested_shutdown = true;
		// 	serverProcess.terminate();
		// };

		if (std::signal(SIGINT, SIGINT_Callback) == SIG_ERR) {
			spdlog::error("Failed to register signal handler.");
			throw std::runtime_error("Failed to register signal handler.");
		}

		// Run the io_context to process the asynchronous I/O operations
		io_context.run();

		// Wait for the process to exit and get the exit status
		serverProcess.wait();
		return serverProcess.exit_code();
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
			spdlog::debug("Starting Wingman with inference port: {}, API/websocket port: {}, gpu layers: {}", params.port, params.websocketPort, params.gpuLayers);
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
				//  when the app exited. If so, we will stop all inference and allow
				//  the UI to determine if the user wants to start another AI model.
				//// If so, we will try the last item with a status
				////  of `complete`, and set that item to `inferring` and restart the app.

				auto appItem = actionsFactory.app()->get("WingmanService");
				if (appItem) {
					bool isInferring = false;
					nlohmann::json j = nlohmann::json::parse(appItem.value().value);
					auto wingmanServerItem = j.get<wingman::WingmanServiceAppItem>();
					spdlog::debug("WingmanServiceAppItem status at last exit: {}", wingman::WingmanServiceAppItem::toString(wingmanServerItem.status));

					if (wingmanServerItem.status == wingman::WingmanServiceAppItemStatus::inferring ||
						wingmanServerItem.status == wingman::WingmanServiceAppItemStatus::preparing) {
						// stop all inference
						auto activeItems = actionsFactory.wingman()->getAllActive();
						for (auto &item : activeItems) {
							std::string error = "Exited during inference. Likely out of GPU memory.";
							if (item.status == wingman::WingmanItemStatus::inferring) {
								item.status = wingman::WingmanItemStatus::error;
								item.error = error;
								actionsFactory.wingman()->set(item);
							}
							if (item.status == wingman::WingmanItemStatus::preparing) {
								item.status = wingman::WingmanItemStatus::error;
								item.error = "Exited during model preparation. Likely out of GPU memory.";
								actionsFactory.wingman()->set(item);
							}
						}
						spdlog::debug("Set {} items to error", activeItems.size());
					}

					// switch (wingmanServerItem.status) {
					// 	case wingman::WingmanServiceAppItemStatus::ready:
					// 		// service was initialized and awaiting a request
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::starting:
					// 		// service was initializing
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::preparing:
					// 		// service is loading and preparing the model (this is usually where an out of memory error would occur)
					// 	case wingman::WingmanServiceAppItemStatus::inferring:
					// 		// service is inferring (an out of memory error could occur here, but it's less likely)
					// 		isInferring = true;
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::stopping:
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::stopped:
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::error:
					// 		break;
					// 	case wingman::WingmanServiceAppItemStatus::unknown:
					// 		break;
					// }
					// if (isInferring) {
					// 	spdlog::debug("WingmanServiceAppItem status was inferring, checking WingmanItem status...");
					// 	// get the last active item and set it to `error`
					// 	auto activeItems = actionsFactory.wingman()->getAllActive();
					// 	if (!activeItems.empty()) {
					// 		spdlog::debug("Found {} items with active status", activeItems.size());
					// 		std::sort(activeItems.begin(), activeItems.end(), [](const wingman::WingmanItem &a, const wingman::WingmanItem &b) {
					// 			return a.updated < b.updated;
					// 		});
					// 		auto lastInferring = activeItems[activeItems.size() - 1];
					// 		lastInferring.status = wingman::WingmanItemStatus::error;
					// 		lastInferring.error = "Exited during inference. Likely out of GPU memory.";
					// 		actionsFactory.wingman()->set(lastInferring);
					// 		spdlog::debug("Set item {} to error", lastInferring.alias);
					// 	}
					// 	spdlog::debug("Checking for last complete item...");
					// 	// get the last item that was `complete`
					// 	auto complete = actionsFactory.wingman()->getByStatus(wingman::WingmanItemStatus::complete);
					// 	if (!complete.empty()) {
					// 		spdlog::debug("Found {} items with status complete", complete.size());
					// 		std::sort(complete.begin(), complete.end(), [](const wingman::WingmanItem &a, const wingman::WingmanItem &b) {
					// 			return a.updated < b.updated;
					// 		});
					// 		auto lastComplete = complete[complete.size() - 1];
					// 		lastComplete.status = wingman::WingmanItemStatus::inferring;
					// 		actionsFactory.wingman()->set(lastComplete);
					// 		spdlog::debug("Set item {} to inferring", lastComplete.alias);
					// 	} else {
					// 		spdlog::debug("No items with status complete found");
					// 	}
					// }
				} else {
					spdlog::debug("WingmanServiceAppItem not found");
				}
			}
		}
	} catch (const std::exception &e) {
		spdlog::error("Wingman Launcher Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("Wingman Launcher exited.");
	return 0;
}
