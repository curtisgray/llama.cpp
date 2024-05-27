#pragma once
#include <optional>

#include "llama.hpp"

namespace wingman::silk::control {
	class ControlServer {
		int controlPort = 6568;
		int inferencePort = 6567;
	public:

		std::shared_ptr<ModelLoader> ai;
		std::function<void()> shutdown;
		std::thread thread;

		ControlServer(int controlPort, int inferencePort = -1);

		~ControlServer();

		bool sendControlHealthRequest() const;
		bool sendInferenceHealthRequest() const;
		bool sendInferenceRestartRequest() const;
		bool sendInferenceStartRequest(const std::string &modelRepo, const std::string &filePath) const;
		bool sendInferenceStartRequest(const std::string &model) const;
		std::optional<nlohmann::json> sendRetrieveModelMetadataRequest() const;
		bool start();
		void stop();
	};
} // namespace wingman::embedding
