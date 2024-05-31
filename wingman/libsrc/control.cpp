#include <vector>
#include <string>

#include <fmt/core.h>

#include "curl.h"
#include "control.h"
#include "wingman.control.h"
#include "spdlog/spdlog.h"

namespace wingman::silk::control {
	WingmanItemStatus inference_status;

	ControlServer::ControlServer(int controlPort, int inferencePort)
		: controlPort(controlPort), inferencePort(inferencePort)
	{
		if (inferencePort == -1) {
			inferencePort = controlPort - 1;
		}
		curl_global_init(CURL_GLOBAL_ALL);
	}

	ControlServer::~ControlServer()
	{
		curl_global_cleanup();
	}

	// Assuming you have a function like this to handle each choice:
	void HandleChoice(const nlohmann::json &choice)
	{
		// Process the choice here, e.g., extract text, finish_reason, etc.
		std::cout << "Choice: " << choice.dump() << std::endl;
	}


	// Function to send the chat completion request to OpenAI and stream the response
	bool ControlServer::sendChatCompletionRequest(
		const OpenAIRequest &request,
		const std::function<void(const std::string &)> &onChunkReceived) const
	{
		bool success = false;

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Construct the request URL
			const std::string url = get_host_url(inferencePort) + "/chat/completions";

			// Convert request parameters to JSON
			const nlohmann::json requestJson = request;

			// Set the URL for the POST request
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			// headers = curl_slist_append(headers, ("Authorization: Bearer " + std::string(OPENAI_API_KEY)).c_str()); // Replace with your actual API key
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			// Set the POST data
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, requestJson.dump().size());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestJson.dump().c_str());
				// Function to handle the streamed response chunks
			const auto writeFunction = +[](void *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				// const auto response = static_cast<std::string *>(userdata);
				const std::function<void(const std::string &)> &chunkReceived = *static_cast<std::function<void(const std::string &)> *>(userdata);
				const auto bytes = static_cast<std::byte *>(contents);
				const auto numBytes = size * nmemb;
				const std::string chunk(reinterpret_cast<const char *>(bytes), numBytes);
				// response->append(reinterpret_cast<const char *>(bytes), numBytes);

				// Split the response into lines
				// const std::vector<std::string> lines = util::splitString(*response, '\n');
				const std::vector<std::string> lines = util::splitString(chunk, '\n');

				// Process each line
				for (const auto &line : lines) {
					// Ignore empty lines and the "data: [DONE]" line
					if (line.empty() || line == "data: [DONE]") {
						continue;
					}

					// Remove the "data: " prefix
					const std::string jsonLine = line.substr(5); // 5 is the length of "data: "

					try {
						// Parse the JSON chunk
						nlohmann::json jsonChunk = nlohmann::json::parse(jsonLine);

						// Extract the 'choices' array
						if (jsonChunk.contains("choices") && jsonChunk["choices"].is_array()) {
							for (const auto &choice : jsonChunk["choices"]) {
								// HandleChoice(choice);
								chunkReceived(choice.dump());
							}
						}
					} catch (const nlohmann::json::parse_error &e) {
						std::cerr << "Error parsing JSON chunk: " << e.what() << std::endl;
						// Handle the error appropriately, e.g., log it or break the loop
					}
				}

				return numBytes;
			};

			// Enable streaming
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);

			// Allocate a string to store the response
			// std::string response;

			// Set the user data for the write callback
			// curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			// curl_easy_setopt(curl, CURLOPT_WRITEDATA, &onChunkReceived);

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << std::endl << "(sendChatCompletionRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				success = true;

				// Process the complete response here if needed
				// std::cout << "Complete response: " << response << std::endl;

				// Split the response into chunks based on the newline character
				// const std::vector<std::string> chunks = util::splitString(response, '\n');
				//
				// // Call the onChunkReceived callback for each chunk
				// for (const auto &chunk : chunks) {
				// 	if (!chunk.empty() && chunk != "data: [DONE]") {
				// 		onChunkReceived(chunk);
				// 	}
				// }
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		return success;
	}

	bool ControlServer::isInferenceRunning(const std::string &modelRepo, const std::string &filePath)
	{
		const auto response = getInferenceStatus();
		if (response) {
			const auto &items = response.value();
			if (!items.empty()) {
				for (const auto &item : items) {
					if (item.modelRepo == modelRepo && item.filePath == filePath) {
						if (item.status == WingmanItemStatus::inferring
							|| item.status == WingmanItemStatus::preparing
							|| item.status == WingmanItemStatus::queued) {
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	bool ControlServer::isInferenceRunning(const std::string &model)
	{
		const auto [modelRepo, filePath] = ModelLoader::parseModelFromMoniker(model);
		return isInferenceRunning(modelRepo, filePath);
	}

	std::optional<std::vector<WingmanItem>> ControlServer::getInferenceStatus() const
	{
		const auto response = sendRequest(get_host_url(controlPort) + "/api/inference/status");
		if (response.has_value()) {
			const std::vector<WingmanItem> items = response->second;
			return items;
		}
		return std::nullopt;
	}

	bool ControlServer::sendControlHealthRequest() const
	{
		const auto response = sendRequest(get_host_url(controlPort) + "/health");
		if (response.has_value()) {
			return response->first == CURLE_OK;
		}
		return false;
	}

	bool ControlServer::sendInferenceHealthRequest() const
	{
		const auto response = sendRequest(get_host_url(inferencePort) + "/health");
		if (response.has_value()) {
			return response->first == CURLE_OK;
		}
		return false;
	}

	bool ControlServer::sendInferenceRestartRequest() const
	{
		const auto response = sendRequest(get_host_url(controlPort) + "/api/inference/restart");
		if (response.has_value()) {
			return response->first == CURLE_OK;
		}
		return false;
	}

	bool ControlServer::sendInferenceStartRequest(const std::string &modelRepo, const std::string &filePath) const
	{
		const auto response = sendRequest(get_host_url(controlPort) +
				   "/api/inference/start?modelRepo=" + modelRepo + "&filePath=" + filePath + "&port=" + std::to_string(inferencePort));
		if (response.has_value()) {
			return response->first == CURLE_OK;
		}
		return false;
	}

	bool ControlServer::sendInferenceStartRequest(const std::string &model) const
	{
		const auto [modelRepo, filePath] = ModelLoader::parseModelFromMoniker(model);
		return sendInferenceStartRequest(modelRepo, filePath);
	}

	bool ControlServer::sendInferenceStopRequest(const std::string &modelRepo, const std::string &filePath) const
	{
		const auto response = sendRequest(get_host_url(controlPort) +
			"/api/inference/stop?modelRepo=" + modelRepo + "&filePath=" + filePath);
		if (response.has_value()) {
			return response->first == CURLE_OK;
		}
		return false;
	}

	bool ControlServer::sendInferenceStopRequest(const std::string &model) const
	{
		const auto [modelRepo, filePath] = ModelLoader::parseModelFromMoniker(model);
		return sendInferenceStopRequest(modelRepo, filePath);
	}

	std::optional<nlohmann::json> ControlServer::sendRetrieveModelMetadataRequest() const
	{
		const auto response = sendRequest(get_host_url(controlPort) + "/api/model/metadata");
		if (response.has_value()) {
			return response->second;
		}
		return std::nullopt;
	}

	std::optional<std::pair<CURLcode, nlohmann::json>> ControlServer::sendRequest(const std::string &url)
	{
		nlohmann::json response;
		CURLcode responseCode = {};

		if (const auto curl = curl_easy_init()) {
			std::string responseBody;
			// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, (url).c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			const auto writeFunction = +[](void *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				const auto body = static_cast<std::string *>(userdata);
				const auto bytes = static_cast<std::byte *>(contents);
				const auto numBytes = size * nmemb;
				body->append(reinterpret_cast<const char *>(bytes), numBytes);
				return size * nmemb;
			};
			// Set write callback function to append data to response_body
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

			// Perform the request
			responseCode = curl_easy_perform(curl);

			// Check for errors
			if (responseCode != CURLE_OK) {
				std::cerr << std::endl << "(sendRequest) cURL failure: " << curl_easy_strerror(responseCode) << std::endl;
			} else {
				if (!responseBody.empty()) {
					// Parse the response body as JSON
					response = nlohmann::json::parse(responseBody);
				}
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		if (responseCode != CURLE_OK) {
			return std::nullopt;
		}
		return std::make_pair(responseCode, response);
	}

	bool ControlServer::start()
	{
		try {
			std::thread inferenceThread([&]() {
				wingman::Start(controlPort, true, true);
			});
			// wait for 60 seconds while checking for the control server status to become
			//	healthy before starting the embedding AI
			bool isHealthy = false;
			for (int i = 0; i < 60; i++) {
				isHealthy = sendControlHealthRequest();
				if (isHealthy) {
					break;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			if (!isHealthy) {
				throw std::runtime_error("Inference server is not healthy");
			}

			thread = std::move(inferenceThread);
			return true;
		} catch (const std::exception &e) {
			spdlog::error("(ControlServer::start) Exception: {}", e.what());
			return false;
		}
	}

	void ControlServer::stop()
	{
		if (thread.joinable()) {
			wingman::RequestSystemShutdown();
			thread.join();
		}
	}
} // namespace wingman::embedding
