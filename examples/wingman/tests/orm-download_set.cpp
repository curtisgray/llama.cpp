#include "../orm.hpp"

namespace wingman::tests {
	void start()
	{
		spdlog::info("Test Start.");

		ItemActionsFactory actionsFactory;
		DownloadItem item;
		item.modelRepo = "TheBloke/Xwin-LM-13B-V0.1-GGUF";
		item.filePath  = "xwin-lm-13b-v0.1.Q2_K.gguf";
		actionsFactory.download()->set(item);
		spdlog::info("Tests Done.");
	}
}

int main()
{
	try {
		spdlog::set_level(spdlog::level::debug);
		wingman::tests::start();
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		return 1;
	}
	spdlog::info("All Tests Done.");
	return 0;
}
