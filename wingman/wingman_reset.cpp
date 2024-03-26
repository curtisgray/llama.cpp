// #define DISABLE_LOGGING 1
#include "json.hpp"
#include "orm.h"

int main(const int argc, char **argv)
{
#if DISABLE_LOGGING
	spdlog::set_level(spdlog::level::off);
#else
	spdlog::set_level(spdlog::level::debug);
#endif
	// const bool alwayReset = argc > 1 && std::string(argv[1]) == "--always";
	const bool alwaysReset = false;
	try {
		const std::string appItemName = "WingmanService";
		spdlog::info("***Wingman Reset Started***");
		wingman::orm::ItemActionsFactory actionsFactory;
		auto appItem = actionsFactory.app()->get(appItemName);
		if (appItem) {
			bool isInferring = false;
			nlohmann::json j = nlohmann::json::parse(appItem.value().value);
			auto wingmanServerItem = j.get<wingman::WingmanServiceAppItem>();
			spdlog::debug("WingmanServiceAppItem status at last exit: {}", wingman::WingmanServiceAppItem::toString(wingmanServerItem.status));
			auto error = wingmanServerItem.error.has_value() ? wingmanServerItem.error.value() : "";
			auto isError1024 = error.find("error code 1024") != std::string::npos;
			if (!isError1024) {	// error code 1024 indicates the server exited cleanly. no further action needed.
				if (alwaysReset
				|| wingmanServerItem.status == wingman::WingmanServiceAppItemStatus::inferring
				|| wingmanServerItem.status == wingman::WingmanServiceAppItemStatus::preparing
				|| wingmanServerItem.status == wingman::WingmanServiceAppItemStatus::error
				) {
				// stop all inference
					auto activeItems = actionsFactory.wingman()->getAllActive();
					for (auto &item : activeItems) {
						std::string error = "Exited during inference. Likely out of GPU memory.";
						if (item.status == wingman::WingmanItemStatus::inferring) {
							item.status = wingman::WingmanItemStatus::error;
							item.error = error;
							actionsFactory.wingman()->set(item);
							spdlog::debug("Set item to error because Wingman service  was actively inferring: {}", item.alias);
						}
						if (item.status == wingman::WingmanItemStatus::preparing) {
							item.status = wingman::WingmanItemStatus::error;
							item.error = "Exited during model preparation. Likely out of GPU memory.";
							actionsFactory.wingman()->set(item);
							spdlog::debug("Set item to error because Wingman service  was preparing inference: {}", item.alias);
						}
					}
					spdlog::debug("Set {} items to error", activeItems.size());
				} else {
					spdlog::debug("Wingman service was not inferring at exit, therefore there is nothing to do.");
				}
			} else {
				spdlog::debug("Wingman service exited cleanly. No further action needed.");
			}
		} else {
			spdlog::debug("WingmanServiceAppItem: {} not found", appItemName);
		}
	} catch (const std::exception &e) {
		spdlog::error("Wingman Reset Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("***Wingman Reset exited***");
	return 0;
}
