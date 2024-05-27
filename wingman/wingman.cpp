
/**
 * @file wingman.cpp
 * @author Curtis Gray (curtis@electricpipelines.com)
 * @brief 
 * @version 0.1
 * @date 2024-05-08
 * 
 * @copyright Copyright (c) 2024 Curtis Gray
 * 
 */

#include <iostream>
#include <string>

#include <spdlog/spdlog.h>

#include "exceptions.h"
#include "wingman.control.h"

struct Params {
	int port = 6567;
	int websocketPort = 6568;
	int gpuLayers = -1;
	std::string logLevel = "debug";
};

static void ParseParams(int argc, char **argv, Params &params)
{
	std::string arg;
	bool invalidParam = false;

	for (int i = 1; i < argc; i++) {
		arg = argv[i];
		if (arg == "--port") {
			if (++i >= argc) {
				invalidParam = true;
				break;
			}
			params.port = std::stoi(argv[i]);
		} else if (arg == "--gpu-layers" || arg == "-ngl" || arg == "--n-gpu-layers") {
			if (++i >= argc) {
				invalidParam = true;
				break;
			}
			params.gpuLayers = std::stoi(argv[i]);
		} else if (arg == "--websocket-port") {
			if (++i >= argc) {
				invalidParam = true;
				break;
			}
			params.websocketPort = std::stoi(argv[i]);
		} else if (arg == "--log-level") {
			if (++i >= argc) {
				invalidParam = true;
				break;
			}
			// ensure log level is valid
			std::string_view level = argv[i];
			if (std::find(std::begin(spdlog::level::level_string_views), std::end(spdlog::level::level_string_views), level) != std::end(spdlog::level::level_string_views)) {
				params.logLevel = std::string(level);
			} else {
				std::cerr << "Invalid log level: " << argv[i] << std::endl;
				std::cerr << "Setting log level to info by default" << std::endl;
				params.logLevel = "info";
			}
		} else if (arg == "--help" || arg == "-?") {
			std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
			std::cout << "Options:" << std::endl;
			std::cout << "  --port <port>            Port to listen on (default: 6567)" << std::endl;
			std::cout << "  --websocket-port <port>  Websocket port to listen on (default: 6568)" << std::endl;
			std::cout << "  --gpu-layers <count>     Number of layers to run on the GPU (default: -1)" << std::endl;
			std::cout << "  --help, -?               Show this help message" << std::endl;
			throw wingman::SilentException();
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}

	if (invalidParam) {
		throw std::runtime_error("invalid parameter for argument: " + arg);
	}
}

int main(const int argc, char **argv)
{
	wingman::argv0 = argv[0];

	auto params = Params();

	ParseParams(argc, argv, params);

	spdlog::set_level(spdlog::level::from_str(params.logLevel));

	try {
		spdlog::info("***Wingman Start***");
		wingman::ResetAfterCrash();
		wingman::Start(params.websocketPort);
		spdlog::info("***Wingman Exit***");
		return 0;
	} catch (const wingman::ModelLoadingException &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		spdlog::error("Error loading model. Restarting...");
		wingman::RequestSystemShutdown();
		spdlog::error("***Wingman Error Exit***");
		return 3;
	} catch (const wingman::SilentException &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		spdlog::error("***Wingman Error Exit***");
		return 0;
	} catch (const std::exception &e) {
		spdlog::error("Exception: " + std::string(e.what()));
		spdlog::error("***Wingman Error Exit***");
		return 1;
	}
}
