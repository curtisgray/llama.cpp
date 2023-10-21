
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>

#include "orm.h"
#include "util.hpp"
#include "curl.h"
#include "download.service.h"
#include "wingman.inference.h"
#include "wingman.service.h"
#include "uwebsockets/App.h"
#include "uwebsockets/Loop.h"

#define LOG_ERROR(MSG, ...) server_log("ERROR", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_WARNING(MSG, ...) server_log("WARNING", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_INFO(MSG, ...) server_log("INFO", __func__, __LINE__, MSG, __VA_ARGS__)

std::atomic requested_shutdown = false;
std::filesystem::path logs_dir;

namespace wingman {
	std::function<void(int)> shutdown_handler;
	void SignalCallback(const int signal)
	{
		shutdown_handler(signal);
	}

	std::function<void(us_timer_t *)> us_timer_handler;
	void UsTimerCallback(us_timer_t *t)
	{
		us_timer_handler(t);
	}

	wingman::ItemActionsFactory actions_factory;

	static void server_log(const char *level, const char *function, int line, const char *message,
						   const nlohmann::ordered_json &extra)
	{
		nlohmann::ordered_json log{
			{"timestamp", time(nullptr)}, {"level", level}, {"function", function}, {"line", line}, {"message", message},
		};

		if (!extra.empty()) {
			log.merge_patch(extra);
		}

		spdlog::info(log.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
	}

	struct PerSocketData {
		/* Define your user data (currently causes crashes... prolly bc of a corking?? problem) */
	};

	//static std::map<std::string_view, uWS::WebSocket<false, true, PerSocketData> *> websocket_connections;
	static std::vector<uWS::WebSocket<false, true, PerSocketData> *> websocket_connections;
	std::mutex websocket_connections_mutex;

	static void UpdateWebsocketConnections(const std::string_view action, uWS::WebSocket<false, true, PerSocketData> *ws)
	{
		std::lock_guard lock(websocket_connections_mutex);
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

	static size_t GetWebsocketConnectionCount()
	{
		std::lock_guard lock(websocket_connections_mutex);
		return websocket_connections.size();
	}

	static void WriteTimingMetricsToFile(const nlohmann::json &metrics, const std::string_view action = "append")
	{
		// std::lock_guard<std::mutex> lock(websocket_connections_mutex);
		// append the metrics to the timing_metrics.json file
		const auto outputFile = logs_dir / std::filesystem::path("timing_metrics.json");

		if (action == "restart") {
			std::filesystem::remove(outputFile);
			WriteTimingMetricsToFile(metrics, "start");
			return;
		}

		std::ofstream timingMetricsFile(outputFile, std::ios_base::app);
		if (action == "start") {
			timingMetricsFile << "[" << std::endl;
		} else if (action == "stop") {
			timingMetricsFile << metrics.dump() << "]" << std::endl;
		} else if (action == "append") {
			timingMetricsFile << metrics.dump() << "," << std::endl;
		}
		timingMetricsFile.close();
	}

	// static json timing_metrics;
	constexpr int max_payload_length = 16 * 1024;
	constexpr int max_backpressure = max_payload_length * 256;
	// ReSharper disable once CppInconsistentNaming
	typedef uWS::WebSocket<false, true, PerSocketData>::SendStatus SendStatus;

	static void SendMetrics(const nlohmann::json &metrics)
	{
		std::lock_guard lock(websocket_connections_mutex);
		static SendStatus lastSendStatus = SendStatus::SUCCESS;
		// loop through all the websocket connections and send the timing metrics
		for (const auto ws : websocket_connections) {
			const auto bufferedAmount = ws->getBufferedAmount();
			const auto remoteAddress = ws->getRemoteAddressAsText();
			try {
				// TODO: deal with backpressure. app will CRASH if too much.
				//   compare buffered_amount to maxBackpressure. if it's too high, wait
				//   for it to drain
				// last_send_status = ws->send(metrics.dump(), uWS::OpCode::TEXT, true);
				if (lastSendStatus == SendStatus::BACKPRESSURE) {
					// if we're still in backpressure, don't send any more metrics
					if (bufferedAmount > max_backpressure / 2) {
						continue;
					}
				}

				lastSendStatus = ws->send(metrics, uWS::OpCode::TEXT, true);

			} catch (const std::exception &e) {
				LOG_ERROR("error sending timing metrics to websocket", {
						  {"remote_address", remoteAddress},
						  {"buffered_amount", bufferedAmount},
						  {"exception", e.what()},
						  });
			}
		}

		WriteTimingMetricsToFile(metrics);
	}

	bool SendServiceStatus(const char *serverName)
	{
		const auto appOption = actions_factory.app()->get(serverName, "default");
		if (appOption) {
			const auto &app = appOption.value();
			nlohmann::json appData = nlohmann::json::parse(app.value, nullptr, false);
			if (!appData.is_discarded())
				SendMetrics(nlohmann::json{ { app.name, appData } }.dump());
			else
				LOG_ERROR("error parsing app data", {
					  {"app_name", app.name},
					  {"app_data", app.value},
					  });
		}
		return !requested_shutdown;
	}

	bool OnDownloadProgress(const wingman::curl::Response *response)
	{
		std::cerr << fmt::format(
			std::locale("en_US.UTF-8"),
			"{}: {} of {} ({:.1f})\t\t\t\t\r",
			response->file.item->modelRepo,
			wingman::util::prettyBytes(response->file.totalBytesWritten),
			wingman::util::prettyBytes(response->file.item->totalBytes),
			response->file.item->progress);

		SendMetrics(nlohmann::json{ { "downloads", { *response->file.item } } });

		return !requested_shutdown;
	}
	//std::function<void(wingman::curl::Response *)> onDownloadProgressHandler = onDownloadProgress;

	bool OnDownloadServiceStatus(wingman::DownloadServerAppItem *)
	{
		return SendServiceStatus("DownloadService");
	}

	bool OnInferenceProgress(const nlohmann::json &metrics)
	{
		SendMetrics(metrics);
		return !requested_shutdown;
	}

	bool OnInferenceServiceStatus(wingman::WingmanServerAppItem *)
	{
		return SendServiceStatus("WingmanService");
	}

	void SendModels(uWS::HttpResponse<false> *res, const uWS::HttpRequest &req)
	{
		res->writeHeader("Content-Type", "application/json; charset=utf-8");
		//res->writeHeader("Access-Control-Allow-Origin", "*");
		//res->writeHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		//res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
		//res->writeHeader("Access-Control-Max-Age", "86400");
		//res->writeHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		//res->writeHeader("Pragma", "no-cache");
		//res->writeHeader("Expires", "0");

		const auto aiModels = curl::getAIModels();
		const auto models = nlohmann::json{ { "models", aiModels } };
		res->end(models.dump());
	}

	void LaunchWebsocketServer(std::string hostname, int websocketPort)
	{
		uWS::App uwsApp =
			uWS::App()
			.ws<PerSocketData>("/*", { .maxPayloadLength = max_payload_length,
			   .maxBackpressure = max_backpressure,
			   .open = [](auto *ws) {
				   /* Open event here, you may access ws->getUserData() which
					   * points to a PerSocketData struct. (except it crashes the webservice randomly when used - CLRG)
					   */
					UpdateWebsocketConnections("add", ws);

					spdlog::info("New connection from remote address {}. Connection count is {}",
						ws->getRemoteAddressAsText(), GetWebsocketConnectionCount());
			   },
			   .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
				   /* Exit gracefully if we get a closedown message */
				   if (message == "shutdown") {
						/* Bye bye */
						ws->send("Shutting down", opCode, true);
						UpdateWebsocketConnections("clear", ws);
						ws->close();
						requested_shutdown = true;
				   } else {
					   /* Log message */
						spdlog::info("Message from {} : {}", ws->getRemoteAddressAsText(), message);
				   }
			   },
			   .close = [](auto *ws, int /*code*/, std::string_view /*message*/) {
				   /* You may access ws->getUserData() here, but sending or
					   * doing any kind of I/O with the socket is not valid. */

				   UpdateWebsocketConnections("remove", ws);
			   } })
			.get("/*", [](auto *res, auto *req) {
				const auto path = util::stringRightTrimCopy(util::stringLower(std::string(req->getUrl())), "/");
				const auto method = util::stringLower(std::string(req->getMethod()));
			   if (method == "get") {
				   if (path == "/api/models")
					   SendModels(res, *req);
				   else {
					   res->writeStatus("404 Not Found");
					   res->end();
				   }
			   } else {
				   res->writeStatus("405 Method Not Allowed");
				   res->end();
			   }
		   })
			.listen(websocketPort, [&](const auto *listenSocket) {
				   if (listenSocket) {
					   printf("\nWingman websocket accepting connections on ws://%s:%d\n\n",
							  hostname.c_str(), websocketPort);
					   spdlog::info("Wingman websocket accepting connections on ws://{}:{}", hostname, websocketPort);
				   } else {
					   spdlog::error("Wingman websocket failed to listen on ws://{}:{}", hostname, websocketPort);
				   }
			   });

			   auto *loop = reinterpret_cast<struct us_loop_t *>(uWS::Loop::get());
			   const auto usAppMetricsTimer = us_create_timer(loop, 0, 0);

			   WriteTimingMetricsToFile({}, "restart");
			   us_timer_handler = [&](us_timer_t * /*t*/) {
				  // check for shutdown
				   if (requested_shutdown) {
					   uwsApp.close();
					   us_timer_close(usAppMetricsTimer);
				   }
			   };
			   us_timer_set(usAppMetricsTimer, UsTimerCallback, 1000, 1000);
			   uwsApp.run();
			   WriteTimingMetricsToFile({}, "stop");
	}

	void Start(const int port, const int websocketPort, const int gpuLayers)
	{
		spdlog::set_level(spdlog::level::debug);

		logs_dir = actions_factory.getLogsDir();

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
		DownloadService downloadService(actions_factory, OnDownloadProgress, OnDownloadServiceStatus);
		std::thread downloadServiceThread(&DownloadService::run, &downloadService);

		WingmanService wingmanService(actions_factory, port, gpuLayers, OnInferenceProgress, OnInferenceServiceStatus);
		std::thread wingmanServiceThread(&WingmanService::run, &wingmanService);

		// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			spdlog::debug(" (start) SIGINT received.");
			// if we have received the signal before, abort.
			if (requested_shutdown) abort();
			// First SIGINT recieved, attempt a clean shutdown
			requested_shutdown = true;
		};

		std::thread awaitShutdownThread([&]() {
			do {
				if (requested_shutdown) {
					downloadService.stop();
					wingmanService.stop();
					stop_inference();
				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
				}
			} while (!requested_shutdown);
		});

		if (const auto res = std::signal(SIGINT, SignalCallback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			return;
		}

		std::cout << "Press Ctrl-C to quit" << std::endl;

		LaunchWebsocketServer("localhost", websocketPort);
		awaitShutdownThread.join();
		downloadServiceThread.join();
		wingmanServiceThread.join();
		spdlog::info("Servers stopped.");
	}

}
struct Params {
	int port = 6567;
	int websocket_port = 6568;
	int gpu_layers = -1;
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

	ParseParams(argc, argv, params);

	try {
		wingman::Start(params.port, params.websocket_port, params.gpu_layers);
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	return 0;
}
