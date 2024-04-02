#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>

// #include <nlohmann/json.hpp>

#include "json.hpp"
#include "types.h"
#include "orm.h"

namespace wingman::services {
	class WingmanService {
		std::atomic<bool> keepRunning = true;

		orm::ItemActionsFactory &actions;
		// ReSharper disable once CppInconsistentNaming
		const std::string SERVER_NAME = "WingmanService";  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
		// ReSharper disable once CppInconsistentNaming
		const int QUEUE_CHECK_INTERVAL = 1000;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
		// const int ACTIVE_STATUS_CHECK_INTERVAL = 100;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)

		void startInference(const WingmanItem &wingmanItem, bool overwrite);

		void updateServiceStatus(const WingmanServiceAppItemStatus& status, std::optional<std::string> error = std::nullopt);

		void initialize() const;

		void requestShutdown();

		std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
		std::function<void(const std::string &alias, const WingmanItemStatus &status)> onInferenceStatus = nullptr;
		std::function<void(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)> onInferenceServiceStatus = nullptr;
		std::function<void()> &requestShutdownInference;
		// WingmanItemStatus lastStatus = WingmanItemStatus::unknown;
		bool hasInferred = false;
		bool isInferring = false;

	public:
		WingmanService(orm::ItemActionsFactory &factory
			, std::function<void()> &requestShutdownInference
			, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress = nullptr
			, const std::function<void(const std::string &alias, const WingmanItemStatus &status)> &onInferenceStatus = nullptr
			, const std::function<void(const WingmanServiceAppItemStatus &status, std::optional<std::string> error)> &onInferenceServiceStatus = nullptr
		);
		void ShutdownInference();

		void run();

		void stop();
	};
} // namespace wingman::services
