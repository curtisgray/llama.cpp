#pragma once

#include "types.h"
#include "orm.h"

class WingmanService {
	std::atomic<bool> keepRunning = true;

	wingman::ItemActionsFactory &actions;
	const std::string SERVER_NAME = "WingmanService";
	const int QUEUE_CHECK_INTERVAL = 1000;

	void startInference(const wingman::WingmanItem &wingmanItem, bool overwrite);

	void updateServerStatus(const wingman::WingmanServerAppItemStatus &status, std::optional<wingman::WingmanItem> wingmanItem = std::nullopt, std
							::optional<std::string> error = std::nullopt) const;

	void initialize() const;

	std::function<void(wingman::WingmanItem *)> onInferenceProgress = nullptr;

	int port;

	int websocketPort;

	int gpuLayers;

public:
	WingmanService(wingman::ItemActionsFactory &actions_factory, const std::function<void(wingman::WingmanItem *)> &onInferenceProgress = nullptr);
	WingmanService(wingman::ItemActionsFactory &actions_factory, int port, int websocketPort, int gpuLayers, const std::function<void(wingman::WingmanItem *)> &onInferenceProgress = nullptr);

	void run();

	void stop();

	int getPort() const;

	int getWebsocketPort() const;

	int getGpuLayers() const;
};
