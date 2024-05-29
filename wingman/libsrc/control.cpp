#include <vector>
#include <string>

#include "curl.h"
#include "control.h"
#include "orm.h"
#include "wingman.control.h"

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

	// Function to handle the streamed response chunks
	size_t HandleOpenAIResponse(void *contents, const size_t size, const size_t nmemb, void *userdata)
	{
		// const auto response = static_cast<std::string *>(userdata);
		const std::function<void(const std::string &)> &onChunkReceived = *static_cast<std::function<void(const std::string &)> *>(userdata);
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
						onChunkReceived(choice.dump());
					}
				}
			} catch (const nlohmann::json::parse_error &e) {
				std::cerr << "Error parsing JSON chunk: " << e.what() << std::endl;
				// Handle the error appropriately, e.g., log it or break the loop
			}
		}

		return numBytes;
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
			const std::string url = "http://localhost:" + std::to_string(inferencePort) + "/chat/completions";

			// Convert request parameters to JSON
			const nlohmann::json requestJson = request;

			// Set the URL for the POST request
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			// headers = curl_slist_append(headers, ("Authorization: Bearer " + std::string(OPENAI_API_KEY)).c_str()); // Replace with your actual API key
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

			// Set the POST data
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestJson.dump().c_str());

			// Enable streaming
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HandleOpenAIResponse);

			// Allocate a string to store the response
			// std::string response;

			// Set the user data for the write callback
			// curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &onChunkReceived);

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

	bool ControlServer::sendControlHealthRequest() const
	{
		bool success = false;

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(controlPort) + "/health").c_str());

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				// std::cerr << std::endl << "(sendControlHealthRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
				// spdlog::error("cURL failure: {}", curl_easy_strerror(res));
			} else {
				success = true;
			}
		}
		return success;
	}

	bool ControlServer::sendInferenceHealthRequest() const
	{
		bool success = false;

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(inferencePort) + "/health").c_str());

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				// std::cerr << std::endl << "(sendInferenceHealthRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
				// spdlog::error("cURL failure: {}", curl_easy_strerror(res));
			} else {
				success = true;
			}
		}
		return success;
	}

	bool ControlServer::sendInferenceRestartRequest() const
	{
		bool success = false;

		if (const auto curl = curl_easy_init()) {
		// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(controlPort) + "/api/inference/restart").c_str());

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			if (res != CURLE_OK) {
				std::cerr << std::endl << "(sendInferenceRestartRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				success = true;
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		return success;
	}

	bool ControlServer::sendInferenceStartRequest(const std::string &modelRepo, const std::string &filePath) const
	{
		bool success = false;

		if (const auto curl = curl_easy_init()) {
			// Set the URL for the GET request
			const auto params = "?modelRepo=" + modelRepo + "&filePath=" + filePath + "&port=" + std::to_string(inferencePort);
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(controlPort) + "/api/inference/start" + params).c_str());

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			if (res != CURLE_OK) {
				std::cerr << std::endl << "(sendInferenceStartRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				success = true;
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		return success;
	}

	bool ControlServer::sendInferenceStartRequest(const std::string &model) const
	{
		const auto [modelRepo, filePath] = ModelLoader::parseModelFromMoniker(model);
		return sendInferenceStartRequest(modelRepo, filePath);
	}

	std::optional<nlohmann::json> ControlServer::sendRetrieveModelMetadataRequest() const
	{
		nlohmann::json response;
		std::string responseBody;
		bool success = false;

		if (const auto curl = curl_easy_init()) {
			// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(controlPort) + "/api/model/metadata").c_str());

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
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << std::endl << "(sendRetrieveModelMetadataRequest) cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				if (responseBody.empty()) {
					std::cerr << "(sendRetrieveModelMetadataRequest) Empty response body" << std::endl;
				} else {
					// Parse the response body as JSON
					response = nlohmann::json::parse(responseBody);
					success = true;
				}
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	bool ControlServer::start()
	{
		try {
			std::thread inferenceThread([&]() {
				wingman::Start(controlPort, true);
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
		wingman::RequestSystemShutdown();
		thread.join();
	}
} // namespace wingman::embedding
