#pragma once
#include <string>
#include <fstream>
#include <functional>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "orm.h"
#include "util.hpp"

namespace wingman::curl {
	const std::string HF_MODEL_ENDS_WITH = "-GGUF";
	const std::string HF_MODEL_FILE_EXTENSION = ".gguf";
	const std::string HF_MODEL_URL = "https://huggingface.co";
	const std::string HF_THEBLOKE_MODELS_URL = "https://huggingface.co/api/models?author=TheBloke&search=" + HF_MODEL_ENDS_WITH + "&sort=lastModified&direction=-1&full=full";
	const std::string HF_THEBLOKE_MODEL_URL = HF_MODEL_URL + "/TheBloke";

	// add HF_MODEL_ENDS_WITH to the end of the modelRepo if it's not already there
	std::string UnstripFormatFromModelRepo(const std::string &modelRepo);

	// strip HF_MODEL_ENDS_WITH from the end of the modelRepo if it's there
	std::string StripFormatFromModelRepo(const std::string &modelRepo);

	struct Response;

	//typedef void (*OnProgressEvent)(Response *);

	struct Response {
		std::vector<std::byte> data;
		CURLcode curlCode;
		long statusCode;
		std::map<std::string, std::string, util::ci_less> headers;

		struct ResponseFile {
			std::time_t start;
			std::streamsize totalBytesWritten = 0;
			std::shared_ptr<std::ofstream> handle = nullptr;
			std::shared_ptr<DownloadItem> item = nullptr;
			std::optional<std::string> quantization = std::nullopt;
			std::shared_ptr<orm::DownloadItemActions> actions = nullptr;
			std::function<bool(Response *)>	 onProgress = nullptr;
			bool checkExistsThenExit = false;
			bool fileExists = false;
			bool overwrite = false;
			bool wasCancelled = false;
		} file;

		[[nodiscard]] std::string getContentType()
			const
		{
			const auto contentType = headers.find("Content-Type");
			if (contentType == headers.end())
				throw std::runtime_error("No Content-Type header found.");
			return contentType->second;
		}

		[[nodiscard]] bool hasJson() const
		{
			// check if the content type is json
			const auto contentType = headers.find("Content-Type");
			if (contentType == headers.end())
				return false;
			// check if contentType includes "application/json"
			if (util::stringContains(contentType->second, "application/json"))
				return true;
			return false;
		}

		[[nodiscard]] std::string text() const
		{
			if (data.empty())
				return "";
			return std::string(reinterpret_cast<const char *>(data.data()), data.size());
		}

		[[nodiscard]] nlohmann::json json() const
		{
			return nlohmann::json::parse(text());
		}
	};

	struct Request {
		std::string url;
		std::string method;
		std::map<std::string, std::string, util::ci_less> headers;
		std::string body;

		// setting this will cause the file to be downloaded to the specified path
		struct RequestFile {
			std::shared_ptr<DownloadItem> item = nullptr;
			std::optional<std::string> quantization = std::nullopt;
			std::shared_ptr<orm::DownloadItemActions> actions = nullptr;
			std::function<bool(Response *)> onProgress = nullptr;
			bool checkExistsThenExit = false;
			bool fileExists = false;
			bool overwrite = false;
		} file;
	};

	bool UpdateItemProgress(Response *res);

	Response Fetch(const Request &request);

	Response Fetch(const std::string &url);

	bool RemoteFileExists(const std::string &url);

	nlohmann::json GetRawModels();

	nlohmann::json ParseRawModels(const nlohmann::json &rawModels);

	nlohmann::json GetModels();

	nlohmann::json GetAIModels();

	bool HasAIModel(const std::string &modelRepo, const std::string &filePath);

	nlohmann::json FilterModels(nlohmann::json::const_reference models, const std::string &modelRepo, const std::optional<std::string> &filename = {}, const std::optional<std::string> &quantization = {});

	nlohmann::json GetModelByFilename(const std::string &modelRepo, std::string filename);

	std::optional<nlohmann::json> GetModelByQuantization(const std::string &modelRepo, std::string quantization);

	// filter a list of models that have a particular quantization
	nlohmann::json FilterModelsByQuantization(nlohmann::json::const_reference models, const std::string &quantization);

	nlohmann::json GetModelsByQuantization(const std::string &quantization);

	nlohmann::json GetModelQuantizations(const std::string &modelRepo);
}
