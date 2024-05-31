// ReSharper disable CppInconsistentNaming
#pragma once
#include <optional>

#include "control.h"
#include "json.hpp"
#include "llama_integration.h"
#include "curl.h"

namespace wingman::silk::control {
	// Structure to hold the OpenAI API request parameters
	struct OpenAIRequest {
		std::string model;
		std::vector<nlohmann::json> messages;
		double temperature = 1.0;
		int max_tokens = -1;
		double top_p = 1.0;
		double frequency_penalty = 0.0;
		double presence_penalty = 0.0;
		std::string stop;
		bool stream = true;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OpenAIRequest, model, messages, temperature, max_tokens, top_p, frequency_penalty, presence_penalty, stop, stream);

	struct Message {
		std::string role;
		std::string content;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, role, content);

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
		bool sendInferenceStopRequest(const std::string &modelRepo, const std::string &filePath) const;
		bool sendInferenceStopRequest(const std::string &model) const;
		bool sendChatCompletionRequest(const OpenAIRequest &request, const std::function<void(const std::string &)> &onChunkReceived) const;
		bool isInferenceRunning(const std::string &modelRepo, const std::string &filePath);
		bool isInferenceRunning(const std::string &model);
		std::optional<std::vector<WingmanItem>> getInferenceStatus() const;
		std::optional<nlohmann::json> sendRetrieveModelMetadataRequest() const;
		static std::optional<std::pair<CURLcode, nlohmann::json>> sendRequest(const std::string &url);
		bool start();
		void stop();
	};
} // namespace wingman::embedding
