#include "json.hpp"
#include "orm.h"

int main(const int argc, char **argv)
{
	spdlog::set_level(spdlog::level::debug);

	try {
		const std::string appItemName = "WingmanService";
		spdlog::info("Wingman Reset Started.");
		wingman::orm::ItemActionsFactory actionsFactory;
		auto appItem = actionsFactory.app()->get(appItemName);
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
					std::string error = "Exited during inference. Likely out of GPU memory.";
					if (item.status == wingman::WingmanItemStatus::inferring) {
						item.status = wingman::WingmanItemStatus::error;
						item.error = error;
						actionsFactory.wingman()->set(item);
					}
					if (item.status == wingman::WingmanItemStatus::preparing) {
						item.status = wingman::WingmanItemStatus::error;
						item.error = "Exited during model preparation. Likely out of GPU memory.";
						actionsFactory.wingman()->set(item);
					}
				}
				spdlog::debug("Set {} items to error", activeItems.size());
			} else {
				spdlog::debug("System was not inferring at exit, therefore there is nothing to do.");
			}
		} else {
			spdlog::debug("WingmanServiceAppItem: {} not found", appItemName);
		}
	} catch (const std::exception &e) {
		spdlog::error("Wingman Reset Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("Wingman Reset exited.");
	return 0;
}
