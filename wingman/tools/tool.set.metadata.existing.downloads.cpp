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
		// get download items
		auto downloadItems = actions.download()->getAll();
		for (auto &downloadItem : downloadItems) {
			found = true;
			const auto metadata = wingman::GetModelMetadata(downloadItem.modelRepo, downloadItem.filePath, actions);
			if (metadata) {
				downloadItem.metadata = metadata.value().dump();
				actions.download()->set(downloadItem);
				fmt::print("Model: {}\n{}\n", downloadItem.modelRepo, metadata.value().dump(4));
			}
			else
				fmt::print("Model: {}\nNo metadata found.\n", downloadItem.modelRepo);
		}
		if (!found)
			fmt::print("Nothing found.\n");
	}
}

int main(int argc, char *argv[])
{
	argparse::ArgumentParser program("tool.get.metadata.existing..downloads", "0.1");

	program
		.add_description("Add metadata to existing AIs.");

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
