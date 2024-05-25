#include <vector>
#include <string>

#include "curl.h"
#include "control.h"
#include "orm.h"
#include "wingman.h"

namespace wingman::silk::control {
	orm::ItemActionsFactory actions_factory;
	WingmanItemStatus inference_status;

	ControlServer::ControlServer(int inferencePort): controlPort(inferencePort) {
		curl_global_init(CURL_GLOBAL_ALL);
	}

	ControlServer::~ControlServer() {
		curl_global_cleanup();
	}

	bool ControlServer::SendHealthRequest() const
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
				std::cerr << std::endl << "cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				success = true;
			}
		}
		return success;
	}

	bool ControlServer::SendInferenceRestartRequest()
	{
		bool success = false;

		if (const auto curl = curl_easy_init()) {
			// Set the URL for the GET request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(controlPort) + "/api/inference/restart").c_str());

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			if (res != CURLE_OK) {
				std::cerr << std::endl << "cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				success = true;
			}

			// Cleanup curl handle
			curl_easy_cleanup(curl);
		}

		return success;
	}

	std::optional<nlohmann::json> ControlServer::SendRetrieveModelMetadataRequest()
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
				std::cerr << std::endl << "cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				if (responseBody.empty()) {
					std::cerr << "Empty response body" << std::endl;
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
				wingman::Start(controlPort);
			});

			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			thread = std::move(inferenceThread);
			return true;
		} catch (const std::exception &e) {
			spdlog::error("(Start) Exception: {}", e.what());
			return false;
		}
	}

	void ControlServer::Stop()
	{
		shutdown();
		thread.join();
	}
} // namespace wingman::embedding
