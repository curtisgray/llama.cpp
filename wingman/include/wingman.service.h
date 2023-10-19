#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "types.h"
#include "orm.h"

class WingmanService {
	std::atomic<bool> keepRunning = true;

	wingman::ItemActionsFactory &actions;
	const std::string SERVER_NAME = "WingmanService";
	const int QUEUE_CHECK_INTERVAL = 1000;

	void startInference(const wingman::WingmanItem &wingmanItem, bool overwrite);

	void updateServerStatus(const wingman::WingmanServerAppItemStatus &status, std::optional<wingman::WingmanItem> wingmanItem = std::nullopt, std
							::optional<std::string> error = std::nullopt);

	void initialize() const;

	std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
	std::function<bool(wingman::WingmanServerAppItem *)> onServiceStatus = nullptr;

	int port;

	int gpuLayers;

public:
	WingmanService(wingman::ItemActionsFactory &actions_factory, int port, int gpuLayers
		, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress = nullptr
		, const std::function<bool(wingman::WingmanServerAppItem *)> &onServiceStatus = nullptr);

	void run();

	void stop();

	int getPort() const;

	int getGpuLayers() const;
};
