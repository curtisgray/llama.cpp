#pragma once
#include <string>
#include <fstream>
#include <functional>
#include <curl/curl.h>

#include "json.hpp"
#include "orm.h"
#include "util.hpp"

namespace wingman::curl {
	const std::string HF_MODEL_ENDS_WITH = "-GGUF";
	const std::string HF_MODEL_FILE_EXTENSION = ".gguf";
	const std::string HF_MODEL_URL = "https://huggingface.co";
	// constexpr int HF_MODEL_LIMIT = 200;
	constexpr int HF_MODEL_LIMIT = 1024;
	// const std::string HF_THEBLOKE_MODELS_URL = "https://huggingface.co/api/models?author=TheBloke&search=" + HF_MODEL_ENDS_WITH + "&sort=lastModified&direction=-1&full=full" + "&limit=" + std::to_string(HF_MODEL_LIMIT);
	// const std::string HF_THEBLOKE_MODELS_URL = "https://huggingface.co/api/models?filter=gguf&pipeline_tag=text-generation&direction=-1&full=full&sort=lastModified&search=llama-3&limit=" + std::to_string(HF_MODEL_LIMIT);
	const std::string HF_ALL_MODELS_URL_BASE = "https://huggingface.co/api/models?filter=gguf&pipeline_tag=text-generation&direction=-1&full=full&sort=lastModified";
	const std::string HF_ALL_MODELS_URL = HF_ALL_MODELS_URL_BASE + "&limit=" + std::to_string(HF_MODEL_LIMIT);
	// const std::string HF_MODEL_LEADERBOARD_CSV_URL = "https://gblazex-leaderboard.hf.space/file=output/results.csv";
	// const std::string EQ_MODEL_DATA_URL = "https://eqbench.com/script.js";
	// const std::string HF_MODEL_LEADERBOARD_CSV_URL = "https://data.electricpipelines.com/results.csv";
	const std::string HF_MODEL_LEADERBOARD_CSV_URL = "iq/iq.csv";
	// const std::string EQ_MODEL_DATA_URL = "https://data.electricpipelines.com/script.js";
	const std::string EQ_MODEL_DATA_URL = "iq/eq.js";
	const std::string EQ_MODEL_DATA_PATH_DEV = "../../../../../../ux/assets";
	const std::string EQ_MODEL_DATA_PATH_PROD = "../../..";

	std::string GetHFModelListUrl(int limit = HF_MODEL_LIMIT);

	// add HF_MODEL_ENDS_WITH to the end of the modelRepo if it's not already there
	std::string UnstripFormatFromModelRepo(const std::string &modelRepo);

	// strip HF_MODEL_ENDS_WITH from the end of the modelRepo if it's there
	std::string StripFormatFromModelRepo(const std::string &modelRepo);

	struct Response;

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

	nlohmann::json GetRawModels(int maxModelsToRetrieve);

	nlohmann::json ParseRawModels(const nlohmann::json &rawModels);

	nlohmann::json GetModels(int maxModelsToRetrieve);

	nlohmann::json GetAIModels(orm::ItemActionsFactory &actionsFactory, int maxModelsToRetrieve);

	nlohmann::json GetAIModelsFast(orm::ItemActionsFactory &actionsFactory, int maxModelsToRetrieve);

	// bool HasAIModel(const std::string &modelRepo, const std::string &filePath);

	// nlohmann::json FilterModels(nlohmann::json::const_reference models, const std::string &modelRepo, const std::optional<std::string> &filename = {}, const std::optional<std::string> &quantization = {});

	// nlohmann::json GetModelByFilename(const std::string &modelRepo, std::string filename);

	// std::optional<nlohmann::json> GetModelByQuantization(const std::string &modelRepo, std::string quantization);

	// filter a list of models that have a particular quantization
	// nlohmann::json FilterModelsByQuantization(nlohmann::json::const_reference models, const std::string &quantization);

	// nlohmann::json GetModelsByQuantization(const std::string &quantization);

	// nlohmann::json GetModelQuantizations(const std::string &modelRepo);
}
