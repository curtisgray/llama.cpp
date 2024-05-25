#pragma once
#include <optional>

#include "llama.hpp"

namespace wingman::silk::control {
	class ControlServer {
		int controlPort = 45679;
	public:

		std::shared_ptr<ModelLoader> ai;
		std::function<void()> shutdown;
		std::thread thread;

		ControlServer(int inferencePort);

		~ControlServer();

		std::optional<nlohmann::json> SendRetrieverRequest(const std::string& query);
		bool SendHealthRequest() const;
		bool SendInferenceRestartRequest();
		std::optional<nlohmann::json> SendRetrieveModelMetadataRequest();
		bool start();
		void Stop();
	};
} // namespace wingman::embedding
