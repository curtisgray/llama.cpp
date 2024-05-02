#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "json.hpp"
#include "orm.h"
#include "curl.h"

namespace wingman::tools {
	void Start()
	{
		const auto console = spdlog::stdout_color_mt("console");
		spdlog::set_default_logger(console);
		orm::ItemActionsFactory itemActionsFactory;

		const auto models = curl::GetAIModelsFast(itemActionsFactory);

		std::cout << models.dump(4) << std::endl;
	}
}

int main(const int argc, char *argv[])
{
	argparse::ArgumentParser program("tool.get.ai.models");

	program.add_description("Get list of AI Models in JSON.");

	try {
		program.parse_args(argc, argv);
	} catch (const std::runtime_error &err) {
		std::cerr << err.what() << std::endl;
		std::cerr << program;
		return 1;
	}

	try {
		wingman::tools::Start();
	} catch (const std::exception &e) {
		std::cerr << "Exception: " << std::string(e.what());
		return 2;
	}
	return 0;
}
