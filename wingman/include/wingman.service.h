#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

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

		void startInference(const WingmanItem &wingmanItem, bool overwrite) const;

		void updateServerStatus(const WingmanServiceAppItemStatus &status, std::optional<WingmanItem> wingmanItem = std::nullopt, std
								::optional<std::string> error = std::nullopt);

		void initialize() const;

		std::function<bool(const nlohmann::json &metrics)> onInferenceProgress = nullptr;
		std::function<void(const std::string &alias, const WingmanItemStatus &status)> onInferenceStatus = nullptr;
		std::function<bool(WingmanServiceAppItem *)> onServiceStatus = nullptr;
		WingmanItemStatus lastStatus = WingmanItemStatus::unknown;

	public:
		WingmanService(orm::ItemActionsFactory &factory
			, const std::function<bool(const nlohmann::json &metrics)> &onInferenceProgress = nullptr
			, const std::function<void(const std::string &alias, const WingmanItemStatus &status)> &onInferenceStatus = nullptr
			, const std::function<bool(WingmanServiceAppItem *)> &onServiceStatus = nullptr);

		void run();

		void stop();
	};
} // namespace wingman::services
