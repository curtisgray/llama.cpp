
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>

#include "orm.h"
#include "download.service.h"
#include "wingman.inference.h"
#include "wingman.service.h"
#include "uwebsockets/App.h"
#include "uwebsockets/Loop.h"

#define LOG_ERROR(MSG, ...) server_log("ERROR", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_WARNING(MSG, ...) server_log("WARNING", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_INFO(MSG, ...) server_log("INFO", __func__, __LINE__, MSG, __VA_ARGS__)

std::atomic requestedShutdown = false;
std::filesystem::path logs_dir;

std::function<void(int)> shutdown_handler;
void signal_callback(int signal)
{
	shutdown_handler(signal);
}

std::function<void(us_timer_t *)> us_timer_handler;
void us_timer_callback(us_timer_t *t)
{
	us_timer_handler(t);
}

wingman::ItemActionsFactory actionsFactory;

static void server_log(const char *level, const char *function, int line, const char *message,
					   const nlohmann::ordered_json &extra)
{
	nlohmann::ordered_json log{
		{"timestamp", time(nullptr)}, {"level", level}, {"function", function}, {"line", line}, {"message", message},
	};

	if (!extra.empty()) {
		log.merge_patch(extra);
	}

	const std::string str = log.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	printf("%.*s\n", (int)str.size(), str.data());
	fflush(stdout);
}

struct PerSocketData {
	/* Define your user data (currently causes crashes... prolly bc of a corking?? problem) */
};

//static std::map<std::string_view, uWS::WebSocket<false, true, PerSocketData> *> websocket_connections;
static std::vector<uWS::WebSocket<false, true, PerSocketData> *> websocket_connections;
std::mutex websocket_connections_mutex;

static void update_websocket_connections(const std::string_view action, uWS::WebSocket<false, true, PerSocketData> *ws)
{
	std::lock_guard<std::mutex> lock(websocket_connections_mutex);
	const auto addressOfWs = ws;
	if (action == "add") {
		websocket_connections.push_back(ws);
	} else if (action == "remove") {
		// find the index of the websocket connection by comparing the address
		for (auto it = websocket_connections.begin(); it != websocket_connections.end(); ++it) {
			if (*it == addressOfWs) {
				websocket_connections.erase(it);
				break;
			}
		}
	} else if (action == "clear") {
		// ws may be a nullptr in this case, so we can't use it
		websocket_connections.clear();
	}
}

static size_t get_websocket_connection_count()
{
	std::lock_guard<std::mutex> lock(websocket_connections_mutex);
	return websocket_connections.size();
}

static void write_timing_metrics_to_file(const nlohmann::json &metrics, const std::string_view action = "append")
{
	// std::lock_guard<std::mutex> lock(websocket_connections_mutex);
	// append the metrics to the timing_metrics.json file
	const auto output_file = logs_dir / std::filesystem::path("timing_metrics.json");

	if (action == "restart") {
		std::filesystem::remove(output_file);
		write_timing_metrics_to_file(metrics, "start");
		return;
	}

	std::ofstream timing_metrics_file(output_file, std::ios_base::app);
	if (action == "start") {
		timing_metrics_file << "[" << std::endl;
	} else if (action == "stop") {
		timing_metrics_file << metrics.dump() << "]" << std::endl;
	} else if (action == "append") {
		timing_metrics_file << metrics.dump() << "," << std::endl;
	}
	timing_metrics_file.close();
}

// static json timing_metrics;
const int max_payload_length = 16 * 1024;
const int max_backpressure = max_payload_length * 256;
using SendStatus = uWS::WebSocket<false, true, PerSocketData>::SendStatus;

static void update_timing_metrics(const nlohmann::json &metrics)
{
	std::lock_guard lock(websocket_connections_mutex);
	static SendStatus last_send_status = SendStatus::SUCCESS;
	// loop through all the websocket connections and send the timing metrics
	for (const auto ws : websocket_connections) {
		const auto buffered_amount = ws->getBufferedAmount();
		const auto remote_address = ws->getRemoteAddressAsText();
		try {
			// TODO: deal with backpressure. app will CRASH if too much.
			//   compare buffered_amount to maxBackpressure. if it's too high, wait
			//   for it to drain
			// last_send_status = ws->send(metrics.dump(), uWS::OpCode::TEXT, true);
			if (last_send_status == SendStatus::BACKPRESSURE) {
				// if we're still in backpressure, don't send any more metrics
				if (buffered_amount > max_backpressure / 2) {
					continue;
				}
			}

			last_send_status = ws->send(metrics.dump(), uWS::OpCode::TEXT, true);

		} catch (const std::exception &e) {
			LOG_ERROR("error sending timing metrics to websocket", {
																	   {"remote_address", remote_address},
																	   {"buffered_amount", buffered_amount},
																	   {"exception", e.what()},
																   });
		}
	}

	write_timing_metrics_to_file(metrics);
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

	update_timing_metrics(nlohmann::json{ { "downloads", { *response->file.item } } });

	return !requestedShutdown;
}
//std::function<void(wingman::curl::Response *)> onDownloadProgressHandler = onDownloadProgress;

bool onDownloadServiceStatus(wingman::DownloadServerAppItem *)
{
	const auto appOption = actionsFactory.app()->get("DownloadService", "default");
	if (appOption) {
		const auto &app = appOption.value();
		nlohmann::json app_data = nlohmann::json::parse(app.value, nullptr, false);
		if (!app_data.is_discarded())
			update_timing_metrics(nlohmann::json{ { app.name, app_data } });
		else
			LOG_ERROR("error parsing app data", {
													{"app_name", app.name},
													{"app_data", app.value},
												});
	}
	return !requestedShutdown;
}

bool onInferenceProgress(const nlohmann::json &metrics)
{
	update_timing_metrics(metrics);
	return !requestedShutdown;
}

bool onInferenceServiceStatus(wingman::WingmanServerAppItem *)
{
	const auto appOption = actionsFactory.app()->get("WingmanService", "default");
	if (appOption) {
		const auto &app = appOption.value();
		nlohmann::json app_data = nlohmann::json::parse(app.value, nullptr, false);
		if (!app_data.is_discarded())
			update_timing_metrics(nlohmann::json{ { app.name, app_data } }.dump());
		else
			LOG_ERROR("error parsing app data", {
													{"app_name", app.name},
													{"app_data", app.value},
												});
	}
	return !requestedShutdown;
}

void launch_websocket_server(std::string hostname, int websocket_port)
{
	uWS::App uws_app = 
		uWS::App()
		.ws<PerSocketData>("/*", { .maxPayloadLength = max_payload_length,
								  .maxBackpressure = max_backpressure,
								  .open =
									  [](auto *ws) {
										  /* Open event here, you may access ws->getUserData() which
										   * points to a PerSocketData struct.
										   */

										  update_websocket_connections("add", ws);

										  std::cout << "New connection "
													<< get_websocket_connection_count() << " from "
													<< ws->getRemoteAddressAsText() << std::endl;
									  },
								  .message =
									  [](auto *ws, std::string_view message, uWS::OpCode opCode) {
										  /* Exit gracefully if we get a closedown message */
										  if (message == "shutdown") {
											  /* Bye bye */
											  ws->send("Shutting down", opCode, true);
											  update_websocket_connections("clear", ws);
											  ws->close();
											  //svr.stop();
											// TODO: stop services when a *shutdown* message is received
										  } else {
											  /* Log message */
											  std::cout << "Message from " << ws->getRemoteAddressAsText()
														<< ": " << message << std::endl;
										  }
									  },
								  .close =
									  [](auto *ws, int /*code*/, std::string_view /*message*/) {
										  /* You may access ws->getUserData() here, but sending or
										   * doing any kind of I/O with the socket is not valid. */

										  update_websocket_connections("remove", ws);
									  } })
		.listen(websocket_port, [&](const auto *listen_socket) {
										  if (listen_socket) {
											  printf("\nWingman websocket accepting connections on http://%s:%d\n\n",
													 hostname.c_str(), websocket_port);
											  LOG_INFO("Wingman websocket listening", {
																						  {"hostname", hostname},
																						  {"port", websocket_port},
																					  });
										  } else {
											  fprintf(stderr, "Wingman websocket FAILED to listen on port %d\n", websocket_port);
											  LOG_ERROR("Wingman websocket failed to listen", {
																								  {"hostname", hostname},
																								  {"port", websocket_port},
																							  });
										  }
									  });

	write_timing_metrics_to_file({}, "restart");
	auto *loop = reinterpret_cast<struct us_loop_t *>(uWS::Loop::get());
	const auto usAppMetricsTimer = us_create_timer(loop, 0, 0);

	write_timing_metrics_to_file({}, "restart");
	us_timer_handler = [&](us_timer_t * /*t*/) {
		// check for shutdown
		if (requestedShutdown) {
			uws_app.close();
			us_timer_close(usAppMetricsTimer);
		}
	};
	us_timer_set(usAppMetricsTimer, us_timer_callback, 1000, 1000);
	uws_app.run();
	write_timing_metrics_to_file({}, "stop");
}

void start(int port, int websocketPort, int gpuLayers)
{
	spdlog::set_level(spdlog::level::debug);

	logs_dir = actionsFactory.getLogsDir();

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
	//DownloadService downloadService(actionsFactory, onDownloadProgressHandler);
	DownloadService downloadService(actionsFactory, onDownloadProgress, onDownloadServiceStatus);
	std::thread downloadServiceThread(&DownloadService::run, &downloadService);

	WingmanService wingmanService(actionsFactory, port, gpuLayers, onInferenceProgress, onInferenceServiceStatus);
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

	if (const auto res = std::signal(SIGINT, signal_callback); res == SIG_ERR) {
		spdlog::error(" (start) Failed to register signal handler.");
		return;
	}

	std::cout << "Press Ctrl-C to quit" << std::endl;

	launch_websocket_server("localhost", websocketPort);
	downloadServiceThread.join();
	wingmanServiceThread.join();
	spdlog::info("Servers stopped.");
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
