
// #define USE_BOOST_PROCESS

#include <csignal>
#include <iostream>
#include <filesystem>
#include <fmt/core.h>
// #include <nlohmann/json.hpp>

#ifdef USE_BOOST_PROCESS
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#else
#include <process.hpp>
#endif

#include "json.hpp"
#include "orm.h"
#include "curl.h"
#include "types.h"

namespace wingman {
	using namespace std::chrono_literals;
	// namespace bp = boost::process;
	namespace fs = std::filesystem;

	const std::string SERVER_NAME = "Wingman_Launcher";

	std::filesystem::path logs_dir;

	std::function<void(int)> shutdown_handler;

	orm::ItemActionsFactory actions_factory;

	bool requested_shutdown;

#ifdef _WIN32
	//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
	std::string GetLastErrorAsString()
	{
		//Get the error message ID, if any.
		DWORD errorMessageID = ::GetLastError();
		if (errorMessageID == 0) {
			return std::string(); //No error message has been recorded
		}

		LPSTR messageBuffer = nullptr;

		//Ask Win32 to give us the string version of that message ID.
		//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
									 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

		//Copy the error message into a std::string.
		std::string message(messageBuffer, size);

		//Free the Win32's string's buffer.
		LocalFree(messageBuffer);

		return message;
	}
#endif

	// ReSharper disable once CppInconsistentNaming
	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	bool SendShutdownSignal()
	{
		bool ret = false;
		try {
			const std::string url = fmt::format("http://{}:6568/api/shutdown", DEFAULT_DBARQ_HOST);

			spdlog::debug("Sending shutdown signal to Wingman server at: {}", url);
			curl::Response response = curl::Fetch(url);

			if (response.curlCode == CURLE_OK && response.statusCode == 200) {
				spdlog::info("Shutdown signal sent successfully.");
				ret = true;
			} else {
				spdlog::error("Fetch failed, CURLcode: {}, HTTP status code: {}", static_cast<int>(response.curlCode), response.statusCode);
			}
		} catch (const std::exception &e) {
			spdlog::error("Exception caught: {}", e.what());
		}
		return ret;
	}

#ifdef USE_BOOST_PROCESS
	int Start(const int port, const int websocketPort, const int gpuLayers)
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

			auto start_time = std::chrono::high_resolution_clock::now();
			auto end_time = start_time + std::chrono::seconds(20);
			bool timeout_expired = false;

			while (std::chrono::high_resolution_clock::now() < end_time && serverProcess.running()) {
				// Process any outstanding asynchronous operations or timeouts
				io_context.run_one_for(std::chrono::milliseconds(100));

				// Check if the process has exited after handling the event
				if (!serverProcess.running()) break;
			}

			if (serverProcess.running()) {
				spdlog::warn("Timeout expired. Forcibly terminating the Wingman server process.");
				serverProcess.terminate();
				timeout_expired = true;
			}

			if (!timeout_expired) {
				spdlog::debug("Wingman server process exited before timeout.");
			} else {
				spdlog::debug("Wingman server process was terminated after timeout.");
			}
		};

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
#else
	int Start(const int port, const int websocketPort, const int gpuLayers)
	{
		logs_dir = actions_factory.getLogsDir();
		spdlog::debug("Logs directory: {}", logs_dir.string());
		spdlog::info("Starting Wingman server...");
		// Enhanced logging to verify current working directory.
		spdlog::info("Current Working Directory: {}", fs::current_path().string());

		// Assuming 'wingman' executable
		fs::path executablePath = fs::current_path();
#ifdef _WIN32
		executablePath /= "wingman.exe";
#else
		executablePath /= "wingman";
#endif

		spdlog::debug("Executable path: {}", executablePath.string());
		// Explicity set the current working directory to the executable's directory
		fs::current_path(executablePath.parent_path());
		spdlog::debug("Explicitly set current working directory to: {}", fs::current_path().string());
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
			fs::current_path().string(),
			[](const char *bytes, size_t n) {
				// spdlog::debug("Wingman: {}", util::stringRightTrimCopy(std::string(bytes, n)));
			std::cout << "Wingman: " << std::string(bytes, n);
		},
			[](const char *bytes, size_t n) {
				// spdlog::debug("Wingman (stderr): {}", util::stringRightTrimCopy(std::string(bytes, n)));
			const auto str = std::string(bytes, n);
			if (str == ".")
				std::cerr << str;
			else
				std::cerr << "Wingman (stderr): " << std::string(bytes, n);
		}
		);

#ifdef _WIN32
		auto lastError = GetLastError();
		if (lastError != 0) {
			auto lastErrorString = GetLastErrorAsString();
			spdlog::debug("GetLastError: {}, GetLastErrorAsString: {}", lastError, lastErrorString);
			return lastError;
		}
#endif

		shutdown_handler = [&](int /* signum */) {
			spdlog::debug("SIGINT received. Attempting to shutdown Wingman server gracefully...");

			if (requested_shutdown) abort(); // Prevent double SIGINT causing immediate abort

			requested_shutdown = true;

			auto start_time = std::chrono::high_resolution_clock::now();
			auto end_time = start_time + std::chrono::seconds(20);
			bool timeout_expired = false;
			int exit_status;

			SendShutdownSignal();
			while (std::chrono::high_resolution_clock::now() < end_time &&
				!serverProcess.try_get_exit_status(exit_status)) {

				// Sleep for a short duration to avoid busy-waiting
				std::this_thread::sleep_for(100ms);
			}

			if (!serverProcess.try_get_exit_status(exit_status)) {
				spdlog::warn("Timeout expired. Forcibly terminating the Wingman server process.");
				serverProcess.kill(true);
				timeout_expired = true;
			}

			if (!timeout_expired) {
				spdlog::debug("Wingman server process exited before timeout.");
			} else {
				spdlog::debug("Wingman server process was terminated after timeout.");
			}
		};

		if (std::signal(SIGINT, SIGINT_Callback) == SIG_ERR) {
			spdlog::error("Failed to register signal handler.");
			throw std::runtime_error("Failed to register signal handler.");
		}

		return serverProcess.get_exit_status();
	}
#endif

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
			const int result = wingman::Start(params.port, params.websocketPort, params.gpuLayers);
			if (wingman::requested_shutdown) {
				spdlog::debug("Wingman exited with return value: {}. Shutdown requested...", result);
				break;
			}
			if (result == 3) {
				// 3 is the exit code for error loading the model
				// the server exited cleanly, so we can just restart it

			} else if (result != 0) {
				spdlog::error("Wingman exited with return value: {}", result);
				// when the app exits, we need to check if it was due to an out of memory error
				//  since there's currently no way to detect this from the app itself, we need to
				//  check the WingmanService status in the database to see if inference was running
				//  when the app exited. If so, we will stop all inference and allow
				//  the UI to determine if the user wants to start another AI model.

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
							std::string error = "There is not enough available memory to load the AI model.";
							if (item.status == wingman::WingmanItemStatus::inferring) {
								item.status = wingman::WingmanItemStatus::error;
								item.error = error;
								actionsFactory.wingman()->set(item);
							}
							if (item.status == wingman::WingmanItemStatus::preparing) {
								item.status = wingman::WingmanItemStatus::error;
								item.error = "There is not enough available memory to load the AI model.";
								actionsFactory.wingman()->set(item);
							}
						}
						spdlog::debug("Set {} items to error", activeItems.size());
					}
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
