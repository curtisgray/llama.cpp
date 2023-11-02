#include <string>

#include "orm.h"

struct Params {
	int port = 6567;
	int websocket_port = 6568;
	int gpu_layers = 0;
};

static void ParseParams(int argc, char **argv, Params &params)
{
	std::string arg;
	bool invalid_param = false;

	for (int i = 1; i < argc; i++) {
		arg = argv[i];
		if (arg == "--port") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.port = std::stoi(argv[i]);
		} else if (arg == "--gpu-layers" || arg == "-ngl" || arg == "--n-gpu-layers") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.gpu_layers = std::stoi(argv[i]);
		} else if (arg == "--websocket-port") {
			if (++i >= argc) {
				invalid_param = true;
				break;
			}
			params.websocket_port = std::stoi(argv[i]);
		} else {
			throw std::runtime_error("unknown argument: " + arg);
		}
	}

	if (invalid_param) {
		throw std::runtime_error("invalid parameter for argument: " + arg);
	}
}

int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);

	auto params = Params();

	ParseParams(argc, argv, params);

	const auto port = params.port;
	const auto websocketPort = params.websocket_port;
	const auto gpuLayers = params.gpu_layers;

	return 0;
}
