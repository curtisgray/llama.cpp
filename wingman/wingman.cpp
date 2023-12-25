
#include <csignal>
#include <iostream>
#include <queue>
#include <nlohmann/json.hpp>

#include "orm.h"
#include "util.hpp"
#include "curl.h"
#include "download.service.h"
#include "wingman.service.h"
#include "wingman.server.integration.h"
#include "uwebsockets/App.h"
#include "uwebsockets/Loop.h"

#define LOG_ERROR(MSG, ...) server_log("ERROR", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_WARNING(MSG, ...) server_log("WARNING", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_INFO(MSG, ...) server_log("INFO", __func__, __LINE__, MSG, __VA_ARGS__)

using namespace std::chrono_literals;

const std::string SERVER_NAME = "WingmanApp";

std::atomic requested_shutdown = false;
std::filesystem::path logs_dir;

namespace wingman {
	std::function<void(int)> shutdown_handler;
	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	std::function<void(int)> abort_handler;
	void SIGABRT_Callback(const int signal)
	{
		abort_handler(signal);
	}

	std::function<void(us_timer_t *)> us_timer_handler;
	void UsTimerCallback(us_timer_t *t)
	{
		us_timer_handler(t);
	}

	struct PerSocketData {
		/* Define your user data (currently causes crashes... prolly bc of a corking?? problem) */
	};

	static std::vector<uWS::WebSocket<false, true, PerSocketData> *> websocket_connections;
	static std::queue<nlohmann::json> metrics_send_queue;
	std::mutex metrics_send_queue_mutex;

	// static json timing_metrics;
	constexpr unsigned MAX_PAYLOAD_LENGTH = 256 * 1024;
	constexpr unsigned MAX_BACKPRESSURE = MAX_PAYLOAD_LENGTH * 512;
	// ReSharper disable once CppInconsistentNaming
	typedef uWS::WebSocket<false, true, PerSocketData>::SendStatus SendStatus;

	uWS::Loop *uws_app_loop = nullptr;

	orm::ItemActionsFactory actions_factory;

	// ReSharper disable once CppInconsistentNaming
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

	static void UpdateWebsocketConnections(const std::string_view action, uWS::WebSocket<false, true, PerSocketData> *ws)
	{
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

	void EnqueueMetrics(const nlohmann::json &json)
	{
		std::lock_guard lock(metrics_send_queue_mutex);
		metrics_send_queue.push(json);
	}

	void EnqueueAllMetrics()
	{
		const auto appItems = actions_factory.app()->getAll();
		// filter out all but WingmanService and DownloadService (NOTE: std::views::filter cannot be made const)
		auto publicServices = std::views::filter(appItems, [](const auto &appItem) {
			return appItem.name == "WingmanService" || appItem.name == "DownloadService";
		});
		for (const auto &service : publicServices) {
			const auto appData = nlohmann::json::parse(service.value, nullptr, false);
			if (!appData.is_discarded()) {
				EnqueueMetrics(nlohmann::json{ { service.name, appData } });
			} else {
				LOG_ERROR("error parsing app data", {
										  {"app_name", service.name},
										  {"app_data", service.value},
										  });
			}
		}

		const auto wingmanItems = actions_factory.wingman()->getAll();
		EnqueueMetrics(nlohmann::json{ { "WingmanItems", wingmanItems } });

		// send all downloadItems updated within the last 30 minutes
		const auto downloadItems = actions_factory.download()->getAllSince(30min);
		EnqueueMetrics(nlohmann::json{ { "DownloadItems", downloadItems } });
	}

	void WriteResponseHeaders(uWS::HttpResponse<false> *res)
	{
		res->writeHeader("Content-Type", "application/json; charset=utf-8");
		res->writeHeader("Access-Control-Allow-Origin", "*");
		//res->writeHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		res->writeHeader("Access-Control-Allow-Methods", "GET");
		res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
	}

	void SendCorkedResponseHeaders(uWS::HttpResponse<false> *res)
	{
		res->cork([res]() {
			WriteResponseHeaders(res);
		});
	}

	static void SendMetrics(const nlohmann::json &metrics)
	{
		static SendStatus lastSendStatus = SendStatus::SUCCESS;
		// loop through all the websocket connections and send the timing metrics
		for (const auto ws : websocket_connections) {
			const auto bufferedAmount = ws->getBufferedAmount();
			const auto remoteAddress = ws->getRemoteAddressAsText();
			try {
				lastSendStatus = ws->send(metrics.dump(), uWS::TEXT);
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
				//SendMetrics(nlohmann::json{ { app.name, appData } }.dump());
				EnqueueMetrics(nlohmann::json{ { app.name, appData } });
			else
				LOG_ERROR("error parsing app data", {
					  {"app_name", app.name},
					  {"app_data", app.value},
					  });
		}
		return !requested_shutdown;
	}

	void SendJson(uWS::HttpResponse<false> *res, const nlohmann::json &json)
	{
		WriteResponseHeaders(res);
		const auto contentLength = std::to_string(json.dump().length());
		res->cork([res, json, contentLength]() {
			res->end(json.dump());
			//res->end(json);
		});
	}

	void SendModels(uWS::HttpResponse<false> *res, const uWS::HttpRequest &req)
	{
		nlohmann::json aiModels;
		//constexpr auto fiveMinutes = std::chrono::milliseconds(300s); // 5 minutes
		//constexpr auto thirtySeconds = std::chrono::milliseconds(30s); // 5 minutes
		//// get cached models from the database using the AppItemActions
		//const auto cachedModels = actions_factory.app()->getCached(SERVER_NAME, "aiModels", thirtySeconds);
		//auto useCachedModels = false;
		//if (cachedModels) {
		//	aiModels = nlohmann::json::parse(cachedModels.value().value, nullptr, false);
		//	if (!aiModels.is_discarded()) {
		//		useCachedModels = true;
		//	} else {
		//		spdlog::debug("(SendModels) will send cached models rather than retrieve fresh listing");
		//	}
		//}
		//if (!useCachedModels) {
			aiModels = curl::GetAIModels(actions_factory.download());
			// cache retrieved models
			AppItem appItem;
			appItem.name = SERVER_NAME;
			appItem.key = "aiModels";
			appItem.value = aiModels.dump();
			actions_factory.app()->set(appItem);
		//}

		SendJson(res, nlohmann::json{ { "models", aiModels } });
	}

	void SendDownloadItems(uWS::HttpResponse<false> *res, const uWS::HttpRequest &req)
	{
		const auto downloadItems = actions_factory.download()->getAll();
		const auto metrics = nlohmann::json{ { "DownloadItems", downloadItems } };
		SendJson(res, metrics);
	}

	void SendWingmanItems(uWS::HttpResponse<false> *res, const uWS::HttpRequest &req)
	{
		const auto wingmanItems = actions_factory.wingman()->getAll();
		const auto metrics = nlohmann::json{ { "WingmanItems", wingmanItems } };
		SendJson(res, metrics);
	}

	void EnqueueDownloadItem(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));

		WriteResponseHeaders(res);
		if (modelRepo.empty() || filePath.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			if (!curl::HasAIModel(modelRepo, filePath)) {
				res->writeStatus("404 Not Found");
			} else {
				const auto downloadItems = actions_factory.download()->get(modelRepo, filePath);
				const auto downloadExists = downloadItems &&
					(downloadItems.value().status == DownloadItemStatus::complete
						|| downloadItems.value().status == DownloadItemStatus::downloading
						|| downloadItems.value().status == DownloadItemStatus::queued);
				if (downloadExists) {
					nlohmann::json jdi = downloadItems.value();
					res->writeStatus("208 Already Reported");
					res->write(jdi.dump());
				} else {
					const auto newDownloadItem = actions_factory.download()->enqueue(modelRepo, filePath);
					if (!newDownloadItem) {
						res->writeStatus("500 Internal Server Error");
					} else {
						nlohmann::json jdi = newDownloadItem.value();
						res->writeStatus("202 Accepted");
						res->write(jdi.dump());
					}
				}
			}
		}
		res->end();
	}

	void CancelDownload(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));

		WriteResponseHeaders(res);
		if (modelRepo.empty() || filePath.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			auto di = actions_factory.download()->get(modelRepo, filePath);
			if (!di) {
				res->writeStatus("404 Not Found");
			} else {
				try {
					di.value().status = DownloadItemStatus::cancelled;
					actions_factory.download()->set(di.value());
					const nlohmann::json jdi = di.value();
					res->write(jdi.dump());
				} catch (std::exception &e) {
					spdlog::error(" (CancelDownload) Exception: {}", e.what());
					res->writeStatus("500 Internal Server Error");
				}
			}
		}
		res->end();
	}

	void DeleteDownload(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));

		WriteResponseHeaders(res);
		if (modelRepo.empty() || filePath.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			const auto di = actions_factory.download()->get(modelRepo, filePath);
			if (!di) {
				res->writeStatus("404 Not Found");
			} else {
				try {
					actions_factory.download()->remove(di.value().modelRepo, di.value().filePath);
					const nlohmann::json jdi = di.value();
					res->write(jdi.dump());
				} catch (std::exception &e) {
					spdlog::error(" (DeleteDownload) Exception: {}", e.what());
					res->writeStatus("500 Internal Server Error");
				}
			}
		}
		res->end();
	}

	bool WaitForInferenceToStop(const std::optional<std::string> &alias = std::nullopt, const std::chrono::milliseconds timeout = 120s)
	{
		const auto start = std::chrono::steady_clock::now();
		spdlog::debug(" (WaitForInferenceStop) Waiting {} seconds for inference to stop...", timeout.count() / 1000);
		while (true) {
			std::vector<WingmanItem> wingmanItems;
			if (alias) {
				const auto wi = actions_factory.wingman()->get(alias.value());
				if (wi) {
					wingmanItems.push_back(wi.value());
				}
			} else {
				wingmanItems = actions_factory.wingman()->getAll();
			}
			if (WingmanItem::hasCompletedStatus(wingmanItems)) {
				return true;
			}
			if (std::chrono::steady_clock::now() - start > timeout) {
				spdlog::error(" (WaitForInferenceStop) Timeout waiting for inference items to complete");
				return false;
			}
			std::this_thread::sleep_for(1s);
		}
	}

	bool StartWingman(const std::string &alias, const std::string &modelRepo, const std::string &filePath,
		const std::string &address = "localhost", const int &port = 6567, const int &contextSize = 0,
		const int &gpuLayers = -1)
	{
		try {
			// check if any inference is running before starting a new one (ONLY ONE INFERENCE PER INSTANCE ALLOWED)
			for (auto &item : actions_factory.wingman()->getAll()) {
				if (WingmanItem::hasActiveStatus(item)) {
					spdlog::debug(" (StartInference) Cancelling inference of {}:{}...", item.modelRepo, item.filePath);
					item.status = WingmanItemStatus::cancelling;
					actions_factory.wingman()->set(item);
					if (!WaitForInferenceToStop(item.alias)) {
						spdlog::error(" (StartInference) Timeout waiting for inference to stop");
						return false;
					}
					spdlog::debug(" (StartInference) Cancelled inference of {}:{}.", item.modelRepo, item.filePath);
				}
			}
			WingmanItem wingmanItem;
			wingmanItem.alias = alias;
			wingmanItem.modelRepo = modelRepo;
			wingmanItem.filePath = filePath;
			wingmanItem.status = WingmanItemStatus::queued;
			wingmanItem.address = address.empty() ? "localhost" : address;
			wingmanItem.port = port;
			wingmanItem.contextSize = contextSize;
			wingmanItem.gpuLayers = gpuLayers;
			actions_factory.wingman()->set(wingmanItem);
			const nlohmann::json wi = wingmanItem;
			spdlog::info(" (StartInference) Inference started: {}", wi.dump());
			return true;
		} catch (std::exception &e) {
			spdlog::error(" (StartInference) Exception: {}", e.what());
			return false;
		}
	}

	void StartInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto alias = std::string(req.getQuery("alias"));
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));
		const auto address = std::string(req.getQuery("address"));
		const auto port = std::string(req.getQuery("port"));
		const auto contextSize = std::string(req.getQuery("contextSize"));
		const auto gpuLayers = std::string(req.getQuery("gpuLayers"));

		const auto enQueue = [&]() {
			try {
				// check if any inference is running before starting a new one (ONLY ONE INFERENCE PER INSTANCE ALLOWED)
				for (auto &item : actions_factory.wingman()->getAll()) {
					if (WingmanItem::hasActiveStatus(item)) {
						spdlog::debug(" (StartInference) Cancelling inference of {}:{}...", item.modelRepo, item.filePath);
						item.status = WingmanItemStatus::cancelling;
						actions_factory.wingman()->set(item);
						if (!WaitForInferenceToStop(item.alias)) {
							spdlog::error(" (StartInference) Timeout waiting for inference to stop");
							res->writeStatus("500 Internal Server Error");
							return;
						}
						spdlog::debug(" (StartInference) Cancelled inference of {}:{}.", item.modelRepo, item.filePath);
					}
				}
				WingmanItem wingmanItem;
				wingmanItem.alias = alias;
				wingmanItem.modelRepo = modelRepo;
				wingmanItem.filePath = filePath;
				wingmanItem.status = WingmanItemStatus::queued;
				wingmanItem.address = address.empty() ? "localhost" : address;
				wingmanItem.port = port.empty() ? 6567 : std::stoi(port);
				wingmanItem.contextSize = contextSize.empty() ? 0 : std::stoi(contextSize);
				wingmanItem.gpuLayers = gpuLayers.empty() ? -1 : std::stoi(gpuLayers);
				actions_factory.wingman()->set(wingmanItem);
				const nlohmann::json wi = wingmanItem;
				res->writeStatus("202 Accepted");
				res->write(wi.dump());
				spdlog::info(" (StartInference) Inference started: {}", wi.dump());
			} catch (std::exception &e) {
				spdlog::error(" (StartInference) Exception: {}", e.what());
				res->writeStatus("500 Internal Server Error");
			}
		};
		WriteResponseHeaders(res);
		if (alias.empty() || modelRepo.empty() || filePath.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
			res->write("{}");
			spdlog::error(" (StartInference) Invalid or Missing Parameter(s)");
		} else {
			const auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				if (WingmanItem::hasActiveStatus(wi.value())) {	// inference is already running
					res->writeStatus("208 Already Reported");
					res->write("{}");
					spdlog::error(" (StartInference) Alias {} already inferring", alias);
				} else {
					// check if any inference is running before starting a new one (ONLY ONE INFERENCE PER INSTANCE ALLOWED)
					for (auto &item : actions_factory.wingman()->getAll()) {
						if (WingmanItem::hasActiveStatus(item)) {
							spdlog::debug(" (StartInference) Cancelling inference of {}:{}...", item.modelRepo, item.filePath);
							item.status = WingmanItemStatus::cancelling;
							actions_factory.wingman()->set(item);
							if (!WaitForInferenceToStop(item.alias)) {
								spdlog::error(" (StartInference) Timeout waiting for inference to stop");
								res->writeStatus("500 Internal Server Error");
								return;
							}
							spdlog::debug(" (StartInference) Cancelled inference of {}:{}.", item.modelRepo, item.filePath);
						}
					}
					enQueue();
				}
			} else {
				enQueue();
			}
		}
		res->end();
	}

	void StopInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		WriteResponseHeaders(res);
		const auto alias = std::string(req.getQuery("alias"));

		if (alias.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				try {
					wi.value().status = WingmanItemStatus::cancelling;
					actions_factory.wingman()->set(wi.value());
					//res->writeStatus("202 Accepted");
					if (WaitForInferenceToStop(alias)) {
						res->writeStatus(uWS::HTTP_200_OK);
						const nlohmann::json jwi = wi.value();
						res->write(jwi.dump());
					}
					else
						res->writeStatus("500 Internal Server Error");
				} catch (std::exception &e) {
					spdlog::error(" (StartInference) Exception: {}", e.what());
					res->writeStatus("500 Internal Server Error");
				}
			} else {
				res->writeStatus("404 Not Found");
			}
		}
		res->end();
	}

	void ResetInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		WriteResponseHeaders(res);
		// get all inference items
		// for each item that is not complete, set status to cancelled

		auto wingmanItems = actions_factory.wingman()->getAll();

		if (WingmanItem::hasCompletedStatus(wingmanItems)) {
			res->writeStatus("200 OK");
			res->end();
			return;
		}

		for (auto &wi : wingmanItems) {
			if (WingmanItem::hasActiveStatus(wi)) {
				try {
					wi.status = WingmanItemStatus::cancelling;
					actions_factory.wingman()->set(wi);
				} catch (std::exception &e) {
					spdlog::error(" (ResetInference) Exception: {}", e.what());
					res->writeStatus("500 Internal Server Error");
				}
			}
		}
		// wait for all inference items to be complete, timing out after 30 seconds
		//constexpr auto timeout = std::chrono::seconds(30);
		//bool success;
		//const auto start = std::chrono::steady_clock::now();
		//while (true) {
		//	wingmanItems = actions_factory.wingman()->getAll();
		//	if (WingmanItem::hasCompletedStatus(wingmanItems)) {
		//		success = true;
		//		break;
		//	}
		//	if (std::chrono::steady_clock::now() - start > timeout) {
		//		success = false;
		//		spdlog::error(" (ResetInference) Timeout waiting for inference items to complete");
		//		break;
		//	}
		//	std::this_thread::sleep_for(1s);
		//}
		if (WaitForInferenceToStop())
			res->writeStatus("200 OK");
		else
			res->writeStatus("500 Internal Server Error");
		res->end();
	}

	void SendInferenceStatus(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		//WriteResponseHeaders(res);
		const auto alias = std::string(req.getQuery("alias"));
		std::vector<WingmanItem> wingmanItems;

		if (alias.empty()) {
			// send all inference items
			wingmanItems = actions_factory.wingman()->getAll();
		} else {
			const auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				wingmanItems.push_back(wi.value());
			}
		}
		const nlohmann::json items = wingmanItems;
		SendJson(res, items);

		//if (alias.empty()) {
		//	// send all inference items
		//	auto wingmanItems = actions_factory.wingman()->getAll();
		//	if (!wingmanItems.empty()) {
		//		const nlohmann::json jwi = wingmanItems;
		//		res->write(jwi.dump());
		//	} else {
		//		res->writeStatus("404 Not Found");
		//	}
		//} else {
		//	auto wi = actions_factory.wingman()->get(alias);
		//	if (wi) {
		//		const nlohmann::json jwi = wi.value();
		//		res->write(jwi.dump());
		//	} else {
		//		res->writeStatus("404 Not Found");
		//	}
		//}
		//res->end();
	}

	bool OnDownloadProgress(const curl::Response *response)
	{
		assert(uws_app_loop != nullptr);
		std::cerr << fmt::format(
			std::locale("en_US.UTF-8"),
			"{}: {} of {} ({:.1f})     \t\t\t\t\t\t\r",
			response->file.item->modelRepo,
			util::prettyBytes(response->file.totalBytesWritten),
			util::prettyBytes(response->file.item->totalBytes),
			response->file.item->progress);

		// see note in `OnInferenceProgress` about `defer`
		//uws_app_loop->defer([response]() {
		//	const auto metrics = nlohmann::json{ { "download", { *response->file.item } } };
		//	SendMetrics(metrics);
		//});
		//const auto metrics = nlohmann::json{ { "download", { *response->file.item } } };

		//const nlohmann::json metrics = *response->file.item;
		//EnqueueMetrics(metrics);

		return !requested_shutdown;
	}

	bool OnInferenceProgress(const nlohmann::json &metrics)
	{
		//EnqueueMetrics(metrics);
		return !requested_shutdown;
	}

	std::map<std::string, WingmanItemStatus> alias_status_map;

	void OnInferenceStatus(const std::string &alias, const WingmanItemStatus &status)
	{
		const auto lastStatus = alias_status_map[alias];
		if (lastStatus != status) {
			alias_status_map[alias] = status;
			auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				wi.value().status = status;
				actions_factory.wingman()->set(wi.value());
			}
		}
	}

	void DrainMetricsSendQueue()
	{
		std::lock_guard lock(metrics_send_queue_mutex);
		while (!metrics_send_queue.empty()) {
			const auto metrics = metrics_send_queue.front();
			metrics_send_queue.pop();
			SendMetrics(metrics);
		}
	}

	void WaitForWebsocketServer(std::string hostname, int websocketPort)
	{
		uWS::App uwsApp =
			uWS::App()
			.ws<PerSocketData>("/*", 
			{
				.maxPayloadLength = MAX_PAYLOAD_LENGTH,
				.maxBackpressure = MAX_BACKPRESSURE,

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
				.drain = [](auto *ws) {
					/* Check getBufferedAmount here */
					spdlog::debug("Buffered amount: {}", ws->getBufferedAmount());
					spdlog::info("Drain from {}", ws->getRemoteAddressAsText());
				},
				.close = [](auto *ws, int /*code*/, std::string_view /*message*/) {
					/* You may access ws->getUserData() here, but sending or
					 * doing any kind of I/O with the socket is not valid. */
					UpdateWebsocketConnections("remove", ws);
				}
			})
			.get("/*", [](auto *res, auto *req) {
				const auto path = util::stringRightTrimCopy(util::stringLower(std::string(req->getUrl())), "/");
				const auto method = util::stringLower(std::string(req->getMethod()));
				if (method == "get") {
					if (path == "/api/models")
						SendModels(res, *req);
					else if (path == "/api/downloads")
						SendDownloadItems(res, *req);
					else if (path == "/api/downloads/enqueue")
						EnqueueDownloadItem(res, *req);
					else if (path == "/api/downloads/cancel")
						CancelDownload(res, *req);
					else if (path == "/api/downloads/reset")
						DeleteDownload(res, *req);
					else if (path == "/api/inference")
						SendWingmanItems(res, *req);
					else if (path == "/api/inference/start")
						StartInference(res, *req);
					else if (path == "/api/inference/stop")
						StopInference(res, *req);
					else if (path == "/api/inference/status")
						SendInferenceStatus(res, *req);
					else if (path == "/api/inference/reset")
						ResetInference(res, *req);
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
				spdlog::info(" (start) Shutting down uWebSockets...");
				uwsApp.close();
				us_timer_close(usAppMetricsTimer);
			}
			DrainMetricsSendQueue();
		};
		us_timer_set(usAppMetricsTimer, UsTimerCallback, 1000, 1000);
		/* Every thread has its own Loop, and uWS::Loop::get() returns the Loop for current thread.*/
		uws_app_loop = uWS::Loop::get();
		uwsApp.run();
		WriteTimingMetricsToFile({}, "stop");
	}

	void Start(const int port, const int websocketPort, const int gpuLayers, const bool isCudaOutOfMemoryRestart)
	{
		spdlog::set_level(spdlog::level::debug);

		logs_dir = actions_factory.getLogsDir();

		if (isCudaOutOfMemoryRestart) {
			spdlog::info("Restarting Wingman services...");
			//WriteTimingMetricsToFile({}, "append");
			// When a CUDA out of memory error occurs, it's most likely bc the loaded model was too big
			//  for the GPU's memory. We need to remove the model from the db and either select a default
			//  model, or pick the last completed model. For now we'll go wiht picking the last completed
			const auto wingmanItems = actions_factory.wingman()->getAll();
			 if (!wingmanItems.empty()) {
				// sort the items by updated descending
				//std::sort(wingmanItems.begin(), wingmanItems.end(), [](const auto &a, const auto &b) {
				//	return a.updated > b.updated;
				//});
			 	const auto lastCompletedItem = std::find_if(wingmanItems.rbegin(), wingmanItems.rend(), [](const auto &item) {
			 		return item.status == WingmanItemStatus::complete;
				});
				if (lastCompletedItem != wingmanItems.rend()) {
					spdlog::info(" (start) Restarting inference with last completed model: {}", lastCompletedItem->modelRepo);
					StartWingman(lastCompletedItem->alias, lastCompletedItem->modelRepo, lastCompletedItem->filePath,
						lastCompletedItem->address, lastCompletedItem->port, lastCompletedItem->contextSize, lastCompletedItem->gpuLayers);
				} else {
					//spdlog::info(" (start) Restarting inference with default model");
					// for now we'll do nothing and let the user select a model
				}
			 }
		} else {
			spdlog::info("Starting Wingman services...");
			//WriteTimingMetricsToFile({}, "start");
		}

		// NOTE: all of three of these signatures work for passing the handler to the DownloadService constructor
		//auto handler = [&](const wingman::curl::Response *response) {
		//	std::cerr << fmt::format(
		//		std::locale("en_US.UTF-8"),
		//		"{}: {} of {} ({:.1f})\t\t\t\t\r",
		//		response->file.item->modelRepo,
		//		wingman::util::prettyBytes(response->file.totalBytesWritten),
		//		wingman::util::prettyBytes(response->file.item->totalBytes),
		//		response->file.item->progress);
		//};
		//DownloadService downloadService(actionsFactory, handler);
		//DownloadService downloadService(actionsFactory, onDownloadProgressHandler);
		//services::DownloadService downloadService(actions_factory, OnDownloadProgress, OnDownloadServiceStatus);
		services::DownloadService downloadService(actions_factory, OnDownloadProgress);
		std::thread downloadServiceThread(&services::DownloadService::run, &downloadService);

		//services::WingmanService wingmanService(actions_factory, OnInferenceProgress, OnInferenceStatus, OnInferenceServiceStatus);
		services::WingmanService wingmanService(actions_factory, OnInferenceProgress, OnInferenceStatus);
		std::thread wingmanServiceThread(&services::WingmanService::run, &wingmanService);

		// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			spdlog::debug(" (start) SIGINT received.");
			// if we have received the signal before, abort.
			if (requested_shutdown) abort();
			// First SIGINT recieved, attempt a clean shutdown
			requested_shutdown = true;
		};

		std::thread runtimeMonitoring([&]() {
			do {
				if (requested_shutdown) {
					spdlog::info(" (start) Shutting down services...");
					downloadService.stop();
					wingmanService.stop();
					stop_inference();
					break;
				}

				EnqueueAllMetrics();

				std::this_thread::sleep_for(std::chrono::milliseconds(250));
			} while (true);
			spdlog::debug(" (start) awaitShutdownThread complete.");
		});

		if (const auto res = std::signal(SIGINT, SIGINT_Callback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			return;
		}

		abort_handler = [&](int /* signum */) {
			spdlog::debug(" (start) SIGABRT received.");
			// there's currently no way to determine if this is a "CUDA error", which is due to
			//	running out of GPU memory, or something we need to abort for, so we'll assume
			//	it's a CUDA out of memory error and throw the appropriate exception.
			throw CudaOutOfMemory("CUDA out of memory");
		};

		if (const auto res = std::signal(SIGABRT, SIGABRT_Callback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			return;
		}

		std::cout << "Press Ctrl-C to quit" << std::endl;

		WaitForWebsocketServer("localhost", websocketPort);
		spdlog::trace(" (start) waiting for runtimeMonitoring to join...");
		runtimeMonitoring.join();
		spdlog::debug(" (start) awaitShutdownThread joined.");
		spdlog::trace(" (start) waiting for downloadServiceThread to join...");
		downloadServiceThread.join();
		spdlog::debug(" (start) downloadServiceThread joined.");
		spdlog::trace(" (start) waiting for wingmanServiceThread to join...");
		wingmanServiceThread.join();
		spdlog::debug(" (start) wingmanServiceThread joined.");
		spdlog::debug(" (start) All services stopped.");
	}

}
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
	auto params = Params();
	auto maxRestartCount = 5;
	auto restartCount = 0;

	ParseParams(argc, argv, params);

	try {
		wingman::Start(params.port, params.websocketPort, params.gpuLayers, false);
	} catch (const wingman::CudaOutOfMemory &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		spdlog::error("CUDA out of memory. Restarting...");
		 if (restartCount++ < maxRestartCount) {
			std::this_thread::sleep_for(5s);
			spdlog::info("Restart {} of {}...", restartCount, maxRestartCount);
			wingman::Start(params.port, params.websocketPort, params.gpuLayers, true);
		 } else {
			 spdlog::error("Maximum restart count {} reached. Exiting...", maxRestartCount);
		 }
	}
	catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	return 0;
}
