#include <argparse/argparse.hpp>
#include <fmt/core.h>

#include "json.hpp"
#include "orm.h"
#include "metadata.h"

namespace wingman::tools {
	void Start()
	{
		bool found = false;

		orm::ItemActionsFactory actions;
		const auto downloadItems = actions.download()->getAll();
		std::vector<nlohmann::json> jsonList;
		for (auto &downloadItem : downloadItems) {
			found = true;
			if (downloadItem.metadata.data()) {
				const auto info = GetModelInfo(downloadItem.modelRepo, downloadItem.filePath, actions);
				if (!info)
					continue;
				jsonList.push_back(info.value());
			}
		}
		const nlohmann::json j = jsonList;
		if (!found)
			fmt::print("Nothing found.\n");
		else
			fmt::print("{}\n", j.dump(4));
	}
}

int main(int argc, char *argv[])
{
	argparse::ArgumentParser program("tool.dump.downloads.metadata", "0.1");

	program
		.add_description("Dump metadata of downloaded AIs.");

	try {
		program.parse_args(argc, argv);    // Example: ./main --color orange
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		std::exit(1);
	}

	try {
		wingman::tools::Start();
	} catch (const std::exception &e) {
		std::cerr << "Exception: " << std::string(e.what());
		return 1;
	}
	return 0;
}
