
#include <csignal>
#include <iostream>
#include <queue>
#include <nlohmann/json.hpp>

#include "orm.h"
#include "util.hpp"
#include "curl.h"
#include "download.service.h"
#include "opencl.info.h"
#include "wingman.service.h"
#include "wingman.server.integration.h"
#include "uwebsockets/App.h"
#include "uwebsockets/Loop.h"
#include "opengl.info.h"

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
	std::mutex inference_mutex;

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

		//// send all downloadItems updated within the last 30 minutes
		//const auto downloadItems = actions_factory.download()->getAllSince(30min);
		const auto downloadItems = actions_factory.download()->getAll();
		EnqueueMetrics(nlohmann::json{ { "DownloadItems", downloadItems } });

		long totalGpuMemory = 0;
		long freeGpuMemory = 0;
		if (opengl::GetGpuMemory(totalGpuMemory, freeGpuMemory)) {
			std::string gpuName = opencl::GetGpuName();

			EnqueueMetrics(nlohmann::json{
				{
					"GpuInfo",
					{
						{ "isa", "GpuInfo" },
						{ "totalMemory", totalGpuMemory},
						{ "freeMemory", freeGpuMemory },
						{ "name", gpuName }
					}
				}
			});
		} else {
			EnqueueMetrics(nlohmann::json{
				{
					"GpuInfo",
					{
						{ "isa", "GpuInfo" },
						{ "totalMemory", -1},
						{ "freeMemory", -1},
						{ "name", "" }
					}
				}
			});
		}
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
		auto response = res->cork([res, json, contentLength]() {
			res->end(json.dump());
		});
	}

	void RequestModels(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
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
		//		spdlog::debug("(RequestModels) will send cached models rather than retrieve fresh listing");
		//	}
		//}
		//if (!useCachedModels) {
			aiModels = curl::GetAIModels(actions_factory);
			// cache retrieved models
			AppItem appItem;
			appItem.name = SERVER_NAME;
			appItem.key = "aiModels";
			appItem.value = aiModels.dump();
			actions_factory.app()->set(appItem);
		//}

		SendJson(res, nlohmann::json{ { "models", aiModels } });
	}

	void RequestDownloadItems(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));
		std::vector<DownloadItem> downloadItems;

		const auto allDownloadItems = actions_factory.download()->getAll();
		if (!modelRepo.empty()) {
			if (!filePath.empty()) {
				for (const auto &item : allDownloadItems) {
					if (item.modelRepo == modelRepo && item.filePath == filePath) {
						downloadItems.push_back(item);
					}
				}
			} else {
				for (const auto &item : allDownloadItems) {
					if (item.modelRepo == modelRepo) {
						downloadItems.push_back(item);
					}
				}
			}
		} else {
			if (!filePath.empty()) {
				for (const auto &item : allDownloadItems) {
					if (item.filePath == filePath) {
						downloadItems.push_back(item);
					}
				}
			} else {
				downloadItems = allDownloadItems;
			}
		}
		//downloadItems = actions_factory.download()->getAll();
		//if (downloadItems.empty()) {
		//	res->writeStatus("404 Not Found");
		//	res->end();
		//} else {
		//	const auto metrics = nlohmann::json{ { "DownloadItems", downloadItems } };
		//	SendJson(res, metrics);
		//}
		const auto metrics = nlohmann::json{ { "DownloadItems", downloadItems } };
		SendJson(res, metrics);
	}

	void RequestWingmanItems(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		const auto alias = std::string(req.getQuery("alias"));
		//const auto wingmanItems = actions_factory.wingman()->getAll();
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
		//if (wingmanItems.empty()) {
		//	res->writeStatus("404 Not Found");
		//	res->end();
		//} else {
		//	const auto metrics = nlohmann::json{ { "WingmanItems", wingmanItems } };
		//	SendJson(res, metrics);
		//}
		const auto metrics = nlohmann::json{ { "WingmanItems", wingmanItems } };
		SendJson(res, metrics);
	}

	void RequestEnqueueDownloadItem(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
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

	void RequestCancelDownload(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
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

	void RequestDeleteDownload(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
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

	void EnsureOnlyOneActiveInference()
	{
		const auto activeItems = actions_factory.wingman()->getAllActive();
		if (!activeItems.empty() && activeItems.size() > 1) {
			// ONLY ONE ACTIVE INFERENCE IS ALLOWED AT A TIME
			std::vector<std::string> aliases;
			for (const auto &item : activeItems) {
				aliases.push_back(item.alias);
			}
			std::string joined = util::joinString(aliases, ", ");
			spdlog::error(" (EnsureOnlyOneActiveInference) Found {} active inference items: {}", activeItems.size(), joined);
			throw std::runtime_error("Found multiple active inference items. Shutting down...");
		}
	}

	bool WaitForInferenceToStop(const std::optional<std::string> &alias = std::nullopt, const std::chrono::milliseconds timeout = 30s)
	{
		const auto start = std::chrono::steady_clock::now();
		spdlog::debug(" (WaitForInferenceStop) Waiting {} seconds for inference  of {} to stop...", timeout.count() / 1000, alias.value_or("all"));
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
				spdlog::error(" (WaitForInferenceStop) Timeout waiting for {} inference to stop", alias.value_or("all"));
				return false;
			}
			std::this_thread::sleep_for(1s);
		}
	}

	// returns true if inference was stopped, false if timeout, and nullopt if there was an error
	std::optional<bool> StopInference(const std::string &alias, const std::chrono::milliseconds timeout = 30s)
	{
		if (alias.empty()) {
			spdlog::error(" (StopInference) Alias cannot be empty");
			return std::nullopt;
		}
		auto wi = actions_factory.wingman()->get(alias);
		if (wi) {
			try {
				// return true if inference is already stopped
				if (WingmanItem::hasCompletedStatus(wi.value())) {
					spdlog::info(" (StopInference) Inference already stopped: {}", alias);
					return true;
				}
				wi.value().status = WingmanItemStatus::cancelling;
				actions_factory.wingman()->set(wi.value());
				if (WaitForInferenceToStop(alias, timeout)) {
					spdlog::info(" (StopInference) Inference stopped: {}", alias);
					return true;
				}
				spdlog::error(" (StopInference) Timeout waiting for inference to stop");
				return false;
			} catch (std::exception &e) {
				spdlog::error(" (StopInference) Exception: {}", e.what());
				return std::nullopt;
			}
		}
		spdlog::error(" (StopInference) Alias {} not found", alias);
		return std::nullopt;
	}

	bool StartInference(const std::string &alias, const std::string &modelRepo, const std::string &filePath,
		const std::string &address = "localhost", const int &port = 6567, const int &contextSize = 0,
		const int &gpuLayers = -1)
	{
		EnsureOnlyOneActiveInference();
		try {
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
			spdlog::info(" (StartInference) Inference enqueued: {}", wi.dump());
			return true;
		} catch (std::exception &e) {
			spdlog::error(" (StartInference) Exception: {}", e.what());
			return false;
		}
	}

	void RequestStartInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		// attempt to lock the inference mutex, and return service unavailable if it is already locked
		if (!inference_mutex.try_lock()) {
			res->writeStatus("503 Service Unavailable");
			res->end();
			return;
		}

		EnsureOnlyOneActiveInference();

		const auto alias = std::string(req.getQuery("alias"));
		const auto modelRepo = std::string(req.getQuery("modelRepo"));
		const auto filePath = std::string(req.getQuery("filePath"));
		const auto address = std::string(req.getQuery("address"));
		const auto port = std::string(req.getQuery("port"));
		const auto contextSize = std::string(req.getQuery("contextSize"));
		const auto gpuLayers = std::string(req.getQuery("gpuLayers"));

		WriteResponseHeaders(res);
		if (alias.empty() || modelRepo.empty() || filePath.empty()) {
			spdlog::error(" (StartInference) Invalid or Missing Parameter(s)");
			res->writeStatus("422 Invalid or Missing Parameter(s)");
			res->write("{}");
		} else {
			const auto wi = actions_factory.wingman()->get(alias);
			bool isAlreadyActive = false;
			if (wi && WingmanItem::hasActiveStatus(wi.value())) {	// inference is already running
				spdlog::warn(" (StartInference) Alias {} already active: {}", alias, WingmanItem::toString(wi.value().status));
				res->writeStatus("208 Already Reported");
				const nlohmann::json jwi = wi.value();
				res->write(jwi.dump());
				isAlreadyActive = true;
			}
			if (!isAlreadyActive) {
				bool readyToEnqueue = true;
				const auto activeItems = actions_factory.wingman()->getAllActive();
				if (readyToEnqueue && !activeItems.empty()) {
					const auto result = StopInference(activeItems[0].alias);
					if (!result) {
						spdlog::error(" (StartInference) Failed to stop inference of {}.", activeItems[0].alias);
						res->writeStatus("500 Internal Server Error");
						readyToEnqueue = false;
					} else if (!result.value()) {
						spdlog::error(" (StartInference) Timeout waiting for inference of {} to stop.", activeItems[0].alias);
						res->writeStatus("500 Internal Server Error");
						readyToEnqueue = false;
					}
				}
				if (readyToEnqueue) {
					// check if this model has been downloaded. if not we can't start inference
					const auto di = actions_factory.download()->get(modelRepo, filePath);
					if (!di) {
						spdlog::error(" (StartInference) Model file does not exist: {}:{}", modelRepo, filePath);
						res->writeStatus("404 Not Found");
						res->write("{}");
						readyToEnqueue = false;
					} else if (di.value().status != DownloadItemStatus::complete) {
						spdlog::error(" (StartInference) Model file not downloaded: {}:{}", modelRepo, filePath);
						res->writeStatus("404 Not Found");
						res->write("{}");
						readyToEnqueue = false;
					}
				}
				if (readyToEnqueue) {
					bool enqueued = false;
					const int intPort = port.empty() ? 6567 : std::stoi(port);
					const int intContextSize = contextSize.empty() ? 0 : std::stoi(contextSize);
					const int intGpuLayers = gpuLayers.empty() ? -1 : std::stoi(gpuLayers);
					if (StartInference(alias, modelRepo, filePath, address, intPort, intContextSize, intGpuLayers)) {
						res->writeStatus("202 Accepted");
						const auto newwi = actions_factory.wingman()->get(alias);
						if (newwi) {
							const nlohmann::json jwi = newwi.value();
							res->write(jwi.dump());
							enqueued = true;
						}
					}
					if (!enqueued) {
						res->writeStatus("500 Internal Server Error");
					}
				}
			}
		}
		res->end();
		inference_mutex.unlock();
	}

	void RequestStopInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		WriteResponseHeaders(res);
		const auto alias = std::string(req.getQuery("alias"));

		if (alias.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				const auto result = StopInference(wi.value().alias);
				if (!result) {
					spdlog::error(" (StartInference) Failed to stop inference of {}.", wi.value().alias);
					res->writeStatus("500 Internal Server Error");
				} else if (!result.value()) {
					spdlog::error(" (StartInference) Timeout waiting for inference of {} to stop.", wi.value().alias);
					res->writeStatus("500 Internal Server Error");
				} else {
					res->writeStatus("200 OK");
					const nlohmann::json jwi = wi.value();
					res->write(jwi.dump());
				}
			} else {
				res->writeStatus("404 Not Found");
			}
		}
		res->end();
	}

	void RequestResetInference(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
		WriteResponseHeaders(res);
		const auto alias = std::string(req.getQuery("alias"));

		if (alias.empty()) {
			res->writeStatus("422 Invalid or Missing Parameter(s)");
		} else {
			auto wi = actions_factory.wingman()->get(alias);
			if (wi) {
				const auto result = StopInference(wi.value().alias);
				if (!result) {
					spdlog::error(" (StartInference) Failed to stop inference of {}.", wi.value().alias);
					res->writeStatus("500 Internal Server Error");
				} else if (!result.value()) {
					spdlog::error(" (StartInference) Timeout waiting for inference of {} to stop.", wi.value().alias);
					res->writeStatus("500 Internal Server Error");
				} else {
					actions_factory.wingman()->remove(wi.value().alias);
					res->writeStatus("200 OK");
					const nlohmann::json jwi = wi.value();
					res->write(jwi.dump());
				}
			} else {
				res->writeStatus("404 Not Found");
			}
		}
		res->end();

		// WriteResponseHeaders(res);
		// // get all inference items
		// // for each item that is not complete, set status to cancelled
		//
		// auto wingmanItems = actions_factory.wingman()->getAll();
		//
		// if (WingmanItem::hasCompletedStatus(wingmanItems)) {
		// 	res->writeStatus("200 OK");
		// 	res->end();
		// 	return;
		// }
		//
		// for (auto &wi : wingmanItems) {
		// 	if (WingmanItem::hasActiveStatus(wi)) {
		// 		try {
		// 			wi.status = WingmanItemStatus::cancelling;
		// 			actions_factory.wingman()->set(wi);
		// 		} catch (std::exception &e) {
		// 			spdlog::error(" (ResetInference) Exception: {}", e.what());
		// 			res->writeStatus("500 Internal Server Error");
		// 		}
		// 	}
		// }
		// if (WaitForInferenceToStop())
		// 	res->writeStatus("200 OK");
		// else
		// 	res->writeStatus("500 Internal Server Error");
		// res->end();
	}

	void RequestInferenceStatus(uWS::HttpResponse<false> *res, uWS::HttpRequest &req)
	{
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
		return !requested_shutdown;
	}

	bool OnInferenceProgress(const nlohmann::json &metrics)
	{
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

	void OnInferenceServiceStatus(const WingmanServiceAppItemStatus& status, std::optional<std::string> error)
	{
		auto appItem = actions_factory.app()->get("WingmanService").value_or(AppItem::make("WingmanService"));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto wingmanServerItem = j.get<WingmanServiceAppItem>();
		wingmanServerItem.status = status;
		if (error) {
			wingmanServerItem.error = error;
		}
		nlohmann::json j2 = wingmanServerItem;
		appItem.value = j2.dump();
		actions_factory.app()->set(appItem);
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
				bool isRequestAborted = false;
				res->onAborted([&]() {
					spdlog::debug("  GET request aborted");
					isRequestAborted = true;
				});

				if (path == "/api/models")
					RequestModels(res, *req);
				else if (path == "/api/downloads")
					RequestDownloadItems(res, *req);
				else if (path == "/api/downloads/enqueue")
					RequestEnqueueDownloadItem(res, *req);
				else if (path == "/api/downloads/cancel")
					RequestCancelDownload(res, *req);
				else if (path == "/api/downloads/reset")
					RequestDeleteDownload(res, *req);
				else if (path == "/api/inference")
					RequestWingmanItems(res, *req);
				else if (path == "/api/inference/start")
					RequestStartInference(res, *req);
				else if (path == "/api/inference/stop")
					RequestStopInference(res, *req);
				else if (path == "/api/inference/status")
					RequestInferenceStatus(res, *req);
				else if (path == "/api/inference/reset")
					RequestResetInference(res, *req);
				else {
					res->writeStatus("404 Not Found");
					res->end();
				}
				if (isRequestAborted) {
					res->cork([res]() {
						res->end();
					});
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
			// When a CUDA out of memory error occurs, it's most likely bc the loaded model was too big
			//  for the GPU's memory. We need to remove the model from the db and either select a default
			//  model, or pick the last completed model. For now we'll go wiht picking the last completed
			const auto wingmanItems = actions_factory.wingman()->getAll();
			 if (!wingmanItems.empty()) {
			 	const auto lastCompletedItem = std::find_if(wingmanItems.rbegin(), wingmanItems.rend(), [](const auto &item) {
			 		return item.status == WingmanItemStatus::complete;
				});
				if (lastCompletedItem != wingmanItems.rend()) {
					spdlog::info(" (start) Restarting inference with last completed model: {}", lastCompletedItem->modelRepo);
					StartInference(lastCompletedItem->alias, lastCompletedItem->modelRepo, lastCompletedItem->filePath,
						lastCompletedItem->address, lastCompletedItem->port, lastCompletedItem->contextSize, lastCompletedItem->gpuLayers);
				} else {
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

		services::WingmanService wingmanService(actions_factory, OnInferenceProgress, OnInferenceStatus, OnInferenceServiceStatus);
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

		// abort_handler = [&](int /* signum */) {
		// 	spdlog::debug(" (start) SIGABRT received.");
		// 	// there's currently no way to determine if this is a "CUDA error", which is due to
		// 	//	running out of GPU memory, or something we need to abort for, so we'll assume
		// 	//	it's a CUDA out of memory error and throw the appropriate exception.
		// 	throw CudaOutOfMemory("CUDA out of memory");
		// };
		//
		// if (const auto res = std::signal(SIGABRT, SIGABRT_Callback); res == SIG_ERR) {
		// 	spdlog::error(" (start) Failed to register signal handler.");
		// 	return;
		// }

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
	// auto maxRestartCount = 5;
	// auto restartCount = 0;

	ParseParams(argc, argv, params);

	try {
		wingman::Start(params.port, params.websocketPort, params.gpuLayers, false);
	} catch (const wingman::CudaOutOfMemory &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		spdlog::error("CUDA out of memory. Restarting...");
		 //if (restartCount++ < maxRestartCount) {
			//std::this_thread::sleep_for(5s);
			//spdlog::info("Restart {} of {}...", restartCount, maxRestartCount);
			//wingman::Start(params.port, params.websocketPort, params.gpuLayers, true);
		 //} else {
			// spdlog::error("Maximum restart count {} reached. Exiting...", maxRestartCount);
		 //}
		requested_shutdown = true;
		return 2;
	}
	catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	return 0;
}
