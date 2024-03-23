#include <string>
#include <fstream>
#include <curl/curl.h>
// #include <nlohmann/json.hpp>
#include <rapidcsv.h>

#include "json.hpp"
#include "types.h"
#include "curl.h"
#include "orm.h"
#include "util.hpp"
#include "parse_evals.h"

namespace wingman::curl {
	// add HF_MODEL_ENDS_WITH to the end of the modelRepo if it's not already there
	std::string UnstripFormatFromModelRepo(const std::string &modelRepo)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}
		if (modelRepo.ends_with(HF_MODEL_ENDS_WITH)) {
			return modelRepo;
		}
		return modelRepo + HF_MODEL_ENDS_WITH;
	}

	// strip HF_MODEL_ENDS_WITH from the end of the modelRepo if it's there
	std::string StripFormatFromModelRepo(const std::string &modelRepo)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}
		if (modelRepo.ends_with(HF_MODEL_ENDS_WITH)) {
			return modelRepo.substr(0, modelRepo.size() - HF_MODEL_ENDS_WITH.size());
		}
		return modelRepo;
	}

	bool UpdateItemProgress(Response *res)
	{
		// only update db every 3 seconds
		const auto seconds = util::now() - res->file.item->updated;
		if (seconds < 3)
			return true;
		if (res->file.item->totalBytes == 0) {
			// get the expected file size from the headers
			const auto contentLength = res->headers.find("Content-Length");
			if (contentLength != res->headers.end()) {
				const auto totalBytes = std::stoll(contentLength->second);
				if (totalBytes > 0)
					res->file.item->totalBytes = totalBytes;
			}
		}
		res->file.item->status = DownloadItemStatus::downloading;
		res->file.item->updated = util::now();
		const auto totalBytesWritten = static_cast<long long>(res->file.totalBytesWritten);
		res->file.item->downloadedBytes = totalBytesWritten;
		res->file.item->downloadSpeed = util::calculateDownloadSpeed(res->file.start, totalBytesWritten);
		if (res->file.item->totalBytes > 0)
			res->file.item->progress = static_cast<double>(res->file.item->downloadedBytes) / static_cast<double>(res->file.item->totalBytes) * 100.0;
		else
			res->file.item->progress = -1;

		res->file.actions->set(*res->file.item);
		try {
			if (res->file.onProgress)
				return res->file.onProgress(res);
		} catch (std::exception &e) {
			spdlog::error("onProgress failed: {}", e.what());
		}
		return true;
	}

	Response Fetch(const Request &request)
	{
		Response response;
		CURLcode res;

#pragma region CURL event handlers

		const auto headerFunction = +[](char *ptr, size_t size, size_t nmemb, void *userdata) {
			const auto res = static_cast<Response *>(userdata);
			const auto bytes = reinterpret_cast<std::byte *>(ptr);
			const auto numBytes = size * nmemb;
			const auto header = std::string(reinterpret_cast<char *>(bytes), numBytes);
			// trim any endline characters and split on the first colon
			const auto pos = header.find_first_of(':');
			if (pos != std::string::npos) {
				auto key = header.substr(0, pos);
				auto value = header.substr(pos + 1);
				// trim leading and trailing whitespace from key and value
				key = util::stringTrim(key);
				value = util::stringTrim(value);
				res->headers[key] = value;
			}

			return numBytes;
		};
		const auto writeFunction = +[](char *ptr, size_t size, size_t nmemb, void *userdata) -> unsigned long long {
			const auto res = static_cast<Response *>(userdata);

			res->file.fileExists = true;

			if (res->file.checkExistsThenExit) {
				// exit with CURLE_WRITE_ERROR to stop the download
				return static_cast<unsigned long long>(0);
			}

			const auto bytes = reinterpret_cast<std::byte *>(ptr);
			const auto numBytes = size * nmemb;

			spdlog::trace("Writing {} bytes to response memory", numBytes);
			res->data.insert(res->data.end(), bytes, bytes + numBytes);
			return numBytes;
		};
		const auto writeFileFunction = +[](char *ptr, size_t size, size_t nmemb, void *userdata) {
			const auto res = static_cast<Response *>(userdata);

			res->file.fileExists = true;

			if (res->file.checkExistsThenExit) {
				// exit with CURLE_WRITE_ERROR to stop the download
				return static_cast<std::streamsize>(0);
			}

			const auto bytes = reinterpret_cast<const char *>(ptr);
			const auto numBytes = static_cast<std::streamsize>(size * nmemb);
			std::streamsize bytesWritten = 0;
			if (res->file.handle != nullptr) {
				res->file.handle->write(bytes, numBytes);
				bytesWritten = numBytes;
				res->file.totalBytesWritten += bytesWritten;
				const auto keepGoing = UpdateItemProgress(res);
				if (!keepGoing) {
					// exit with CURLE_WRITE_ERROR to stop the download
					res->file.wasCancelled = true;
					return static_cast<std::streamsize>(0);
				}
			}
			spdlog::trace("Wrote {} bytes to {}", bytesWritten, res->file.handle == nullptr ? "nullptr" : "[handle]");
			return bytesWritten;
		};

#pragma endregion

		if (CURL *curl = curl_easy_init()) {

#ifndef NDEBUG // nasty double negative (means in debug mode)
		//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

			res = curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
			if (request.file.item) {
				spdlog::debug("Downloading item: {}:{}", request.file.item->modelRepo, request.file.item->filePath);
				// verify that an item and actions are passed in along with the item
				if (!request.file.actions) {
					throw std::runtime_error("No actions passed in with the item.");
				}
				if (request.file.quantization) {
					//throw std::runtime_error("No quantization passed in with the item.");
					response.file.quantization = request.file.quantization;
				}
				response.file.start = util::now();
				response.file.item = request.file.item;
				fs::path path;
				if (request.file.quantization) {
					path = request.file.actions->getDownloadItemOutputFilePathQuant(
						request.file.item->modelRepo, request.file.quantization.value());
				} else {
					path = request.file.actions->getDownloadItemOutputPath(
						request.file.item->modelRepo, request.file.item->filePath);
				}
				response.file.overwrite = request.file.overwrite;
				// TODO: implement resume by default. check if the remove file size is greater than what's on disk then resume,
				//	otherwise `response.file.overwrite`
				response.file.handle = std::make_shared<std::ofstream>(path, std::ios::binary);
				if (!response.file.handle) {
					throw std::runtime_error(fmt::format("Failed to open file for writing: {}", path.string()));
				}
				response.file.actions = request.file.actions;
				response.file.onProgress = request.file.onProgress;
				spdlog::trace("Setting up CURLOPT_WRITEFUNCTION to writeFileFunction");
				res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileFunction);
				spdlog::trace("Setting CURLOPT_WRITEDATA to &response");
				res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			} else {
				spdlog::debug("Requesting url: {}", request.url);
				spdlog::trace("Setting up CURLOPT_WRITEFUNCTION to writeFunction");
				res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
				spdlog::trace("Setting CURLOPT_WRITEDATA to &response");
				res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			}
			response.file.checkExistsThenExit = request.file.checkExistsThenExit;
			spdlog::trace("Setting up CURLOPT_HEADERFUNCTION to headerFunction");
			res = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerFunction);
			res = curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
			spdlog::trace("Enabling CURLOPT_AUTOREFERER and CURLOPT_FOLLOWLOCATION");
			res = curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
			res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			if (!request.method.empty()) {
				spdlog::trace("Setting CURLOPT_CUSTOMREQUEST to {}", request.method);
				res = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
			}
			if (!request.headers.empty()) {
				curl_slist *chunk = nullptr;
				for (const auto &[key, value] : request.headers) {
					spdlog::trace("Adding REQUEST header: {}: {}", key, value);
					const auto header = fmt::format("{}: {}", key, value);
					chunk = curl_slist_append(chunk, header.c_str());
				}
				res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
			}
			if (!request.body.empty()) {
				spdlog::trace("Setting CURLOPT_POSTFIELDS to {}", request.body);
				res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
				res = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
			}

			// execute the cURL request
			spdlog::trace("Calling curl_easy_perform");
			response.curlCode = curl_easy_perform(curl);

			// cleanup the file handle
			if (response.file.handle) {
				spdlog::trace("Flusing file handle");
				response.file.handle->flush();
				spdlog::trace("Getting file size on disk");
				const auto fileSizeOnDisk = static_cast<long long>(response.file.handle->tellp());
				// print any error from ftell
				if (fileSizeOnDisk == -1L) {
					spdlog::error("ftell error: {}", strerror(errno));
					fmt::print("ftell error: {}\n", strerror(errno));
				}
				spdlog::trace("fileSizeOnDisk: {}", fileSizeOnDisk);
				if (fileSizeOnDisk != response.file.item->totalBytes) {
					// file did not finish downloading
				}
				spdlog::trace("Closing file handle");
				response.file.handle->close();

				// send a progress update after file is closed
				if (response.file.onProgress) {
					response.file.onProgress(&response);
				}

				auto item =
					response.file.actions->get(response.file.item->modelRepo, response.file.item->filePath);
				if (!item) {
					throw std::runtime_error(
						fmt::format("Failed to get item for modelRepo: {}, filePath: {}",
							response.file.item->modelRepo, response.file.item->filePath));
				}
				spdlog::trace("Setting DownloadItem status");
				item.value().downloadedBytes = fileSizeOnDisk;
				if (response.file.wasCancelled)
					item.value().status = DownloadItemStatus::cancelled;
				else {
					item.value().progress = static_cast<double>(response.file.totalBytesWritten) / static_cast<double>(fileSizeOnDisk) * 100.0;
					if (item.value().progress > 100.0)
						item.value().progress = 100.0;
					if (item.value().progress < 100.0)
						item.value().status = DownloadItemStatus::cancelled;
					else
						item.value().status = DownloadItemStatus::complete;
				}
				item.value().updated = util::now();
				response.file.actions->set(item.value());
				// send last progress update with the new status
				if (response.file.onProgress) {
					response.file.item->status = item.value().status;
					response.file.item->progress = item.value().progress;
					response.file.onProgress(&response);
				}
			}
			spdlog::trace("Getting response code");
			res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
			spdlog::debug("Response code: {}", response.statusCode);
			spdlog::trace("Calling curl_easy_cleanup");
			curl_easy_cleanup(curl);
		} else {
			throw std::runtime_error("Failed to initialize curl");
		}
		spdlog::trace("Returning response");
		return response;
	}

	Response Fetch(const std::string &url)
	{
		const auto request = Request{ url, "GET", {}, {}, {} };
		return Fetch(request);
	}

	bool RemoteFileExists(const std::string &url)
	{
		auto request = Request{ url };
		request.file.checkExistsThenExit = true;
		const auto response = Fetch(request);
		return response.file.fileExists;
	}

	ModelType EmojiToModelType(const std::string &emoji)
	{
		static const std::map<std::string, ModelType> emojiMap = {
			{"🟢", ModelType::pretrained},
			{"🟩", ModelType::continuously_pretrained},
			{"🔶", ModelType::finetuned},
			{"💬", ModelType::chatmodels},
			{"🤝", ModelType::base_merges},
			// Add more mappings as necessary
		};

		auto it = emojiMap.find(emoji);
		if (it != emojiMap.end()) {
			return it->second;
		} else {
			return ModelType::unknown; // Return unknown for unrecognized emojis
		}
	}

	std::vector<ModelIQEval> FetchAndParseModelIQData()
	{
		// URL from which to fetch the CSV data
		const std::string url = HF_MODEL_LEADERBOARD_CSV_URL;

		// Use the Fetch function to get the data
		Request request{ url, "GET" };
		Response response = Fetch(request);

		// Check the response status and content type
		if (response.curlCode != CURLE_OK || response.statusCode != 200) {
			throw std::runtime_error("Failed to fetch data: HTTP status " + std::to_string(response.statusCode));
		}

		// Convert the response data to a std::string
		std::string csvData = response.text();

		// Use rapidcsv to parse the CSV data
		std::stringstream csvStream(csvData);
		// rapidcsv::Document doc(csvStream, rapidcsv::LabelParams(-1, -1));
		rapidcsv::ConverterParams converterParams(true, -1.0, -1); // set all missing values to -1
		rapidcsv::Document doc(csvStream, rapidcsv::LabelParams(), rapidcsv::SeparatorParams(), converterParams);

		// Extract and populate ModelData objects
		std::vector<ModelIQEval> models;
		for (size_t i = 0; i < doc.GetRowCount(); ++i) {
			ModelIQEval model;

			model.evalName = doc.GetCell<std::string>("eval_name", i);
			model.precision = doc.GetCell<std::string>("Precision", i);
			model.type = doc.GetCell<std::string>("Type", i);

			// Convert emoji representation to enum ModelType
			std::string modelTypeEmoji = doc.GetCell<std::string>("T", i);
			model.modelType = EmojiToModelType(modelTypeEmoji);

			model.weightType = doc.GetCell<std::string>("Weight type", i);
			model.architecture = doc.GetCell<std::string>("Architecture", i);
			model.modelLink = doc.GetCell<std::string>("Model", i);
			model.modelNameForQuery = doc.GetCell<std::string>("model_name_for_query", i);
			model.modelSha = doc.GetCell<std::string>("Model sha", i);
			model.averageUp = doc.GetCell<double>("Average ⬆️", i);
			model.mmluPlusArc = doc.GetCell<double>("MMLU+Arc", i);
			model.hubLicense = doc.GetCell<std::string>("Hub License", i);
			model.hubLikes = doc.GetCell<int>("Hub ❤️", i);
			model.hubDownloads = doc.GetCell<int>("Hub 💾", i);
			model.likesPerWeek = doc.GetCell<double>("Likes / Week", i);
			model.likabilityStar = doc.GetCell<double>("Likability 🌟", i);
			model.paramsBillion = doc.GetCell<double>("#Params (B)", i);
			model.availableOnTheHub = doc.GetCell<std::string>("Available on the hub", i) == "True";
			model.recent7Days = doc.GetCell<std::string>("Recent (7 days)", i) == "True";
			model.recent14Days = doc.GetCell<std::string>("Recent (14 days)", i) == "True";
			model.recent21Days = doc.GetCell<std::string>("Recent (21 days)", i) == "True";
			model.arc = doc.GetCell<double>("ARC", i);
			model.hellaSwag = doc.GetCell<double>("HellaSwag", i);
			model.mmlu = doc.GetCell<double>("MMLU", i);
			model.truthfulQa = doc.GetCell<double>("TruthfulQA", i);
			model.winogrande = doc.GetCell<double>("Winogrande", i);
			model.gsm8K = doc.GetCell<double>("GSM8K", i);

			models.push_back(model);
		}

		return models;
	}

	std::optional<ModelIQEval> GetModelIQData(const std::string &modelName, const std::vector<ModelIQEval> &models)
	{
		for (const auto &model : models) {
			// Compare only the part of the model name after the last slash
			// (e.g., "facebook/bart-large-mnli" -> "bart-large-mnli"), so split the model name by slashes and take the last part
			std::vector<std::string> iqmnParts = util::splitString(model.modelNameForQuery, '/');
			std::vector<std::string> mnParts = util::splitString(modelName, '/');
			if (iqmnParts.empty() || mnParts.empty()) {
				continue;
			}
			if (util::stringCompare(iqmnParts.back(), mnParts.back(), false)) {
				return model;
			}
			// if (util::stringCompare(model.modelNameForQuery, modelName, false)) {
			// 	return model;
			// }
		}
		return std::nullopt;
	}

	std::optional< std::pair<EqBenchData, MagiData>> GetModelEQData(const std::string &modelName,
		const std::map<std::string, std::pair<EqBenchData, MagiData>> &modelEQData)
	{
		// Compare only the part of the model name after the last slash
		// (e.g., "facebook/bart-large-mnli" -> "bart-large-mnli"), so split the model name by slashes and take the last part
		const std::vector<std::string> mnParts = util::splitString(modelName, '/');
		if (mnParts.empty()) {
			return std::nullopt;
		}
		for (const auto &[key, value] : modelEQData) {
			std::vector<std::string> eqmnParts = util::splitString(key, '/');
			if (eqmnParts.empty() || mnParts.empty()) {
				continue;
			}
			if (util::stringCompare(eqmnParts.back(), mnParts.back(), false)) {
				return value;
			}
		}
		return std::nullopt;
	}

	std::optional< std::map<std::string, std::pair<EqBenchData, MagiData>> > FetchAndParseModelEQData()
	{
		try {
			const std::string url = EQ_MODEL_DATA_URL;

			// Use the Fetch function to get the data
			Request request{ url, "GET" };
			Response response = Fetch(request);

			// Check the response status and content type
			if (response.curlCode != CURLE_OK || response.statusCode != 200) {
				throw std::runtime_error("Failed to fetch data: HTTP status " + std::to_string(response.statusCode));
			}

			// Convert the response data to a std::string
			std::string input = response.text();

			return parseLeaderboardData(input);
		} catch (std::exception &e) {
			spdlog::error("Failed to fetch and parse model EQ data: {}", e.what());
			return std::nullopt;
		}
	}

	// Model data structure
	struct ModelScore {
		std::string modelName;
		double eqBenchScore; // normalized to 0-100, -1 if missing
		double magiScore;    // normalized to 0-100, -1 if missing
	};

	// Statistical utility functions (assuming scores are normalized and missing values are marked as -1)
	double CalculateMean(const std::vector<double> &scores)
	{
		double sum = 0.0;
		int count = 0;
		for (const double score : scores) {
			if (score >= 0) { // Only include non-missing scores
				sum += score;
				++count;
			}
		}
		return count > 0 ? sum / count : 0.0;
	}

	double CalculateVariance(const std::vector<double> &scores, double mean)
	{
		double sum = 0.0;
		int count = 0;
		for (const double score : scores) {
			if (score >= 0) { // Only include non-missing scores
				sum += (score - mean) * (score - mean);
				++count;
			}
		}
		return count > 1 ? sum / (count - 1) : 0.0;
	}

	double CalculateCorrelation(const std::vector<double> &scores1, const std::vector<double> &scores2)
	{
		const double mean1 = CalculateMean(scores1);
		const double mean2 = CalculateMean(scores2);
		double sum = 0.0;
		double var1 = 0.0;
		double var2 = 0.0;
		int count = 0;

		for (size_t i = 0; i < scores1.size(); ++i) {
			if (scores1[i] >= 0 && scores2[i] >= 0) {
				sum += (scores1[i] - mean1) * (scores2[i] - mean2);
				var1 += (scores1[i] - mean1) * (scores1[i] - mean1);
				var2 += (scores2[i] - mean2) * (scores2[i] - mean2);
				++count;
			}
		}

		if (count > 1) {
			const double denom = std::sqrt(var1) * std::sqrt(var2);
			return denom != 0 ? sum / denom : 0;
		} else {
			return 0;
		}
	}

	double CalculateCombinedEQScore(const ModelScore &model, double meanEqBench, double meanMagi, double correlation)
	{
		double eqBenchWeight = (model.eqBenchScore >= 0) ? 1.0 : 0.0;
		double magiWeight = (model.magiScore >= 0) ? 1.0 : 0.0;

		// Adjust weights based on correlation
		if (model.eqBenchScore >= 0 && model.magiScore >= 0) {
			eqBenchWeight = magiWeight = 0.5 + (correlation / 2); // Simplistic adjustment
		}

		// Predict missing scores based on correlation and the available score
		const double predictedEqBenchScore = (model.eqBenchScore >= 0) ? model.eqBenchScore : (model.magiScore * correlation) + (1 - correlation) * meanEqBench;
		const double predictedMagiScore = (model.magiScore >= 0) ? model.magiScore : (model.eqBenchScore * correlation) + (1 - correlation) * meanMagi;

		// Calculate combined score
		const double combinedScore = (predictedEqBenchScore * eqBenchWeight) + (predictedMagiScore * magiWeight);
		return combinedScore;
	}

	double CalculateModelIQScore(const ModelIQEval &modelEval)
	{
		double iQScore = -1.0;
		int divisor = 1;
		double adder = 0.0;
		if (modelEval.arc > 0.0) {
			adder += modelEval.arc;
			divisor++;
		}
		if (modelEval.hellaSwag > 0.0) {
			adder += modelEval.hellaSwag;
			divisor++;
		}
		if (modelEval.mmlu > 0.0) {
			adder += modelEval.mmlu;
			divisor++;
		}
		if (modelEval.truthfulQa > 0.0) {
			adder += modelEval.truthfulQa;
			divisor++;
		}
		if (modelEval.winogrande > 0.0) {
			adder += modelEval.winogrande;
			divisor++;
		}
		if (modelEval.gsm8K > 0.0) {
			adder += modelEval.gsm8K;
			divisor++;
		}
		if (divisor > 0) {
			iQScore = adder / divisor;
		}

		return iQScore;
	}

	nlohmann::json GetRawModels()
	{
		try {
			spdlog::debug("Fetching models from {}", HF_THEBLOKE_MODELS_URL);
			auto r = Fetch(HF_THEBLOKE_MODELS_URL);
			spdlog::trace("HTTP status code: {}", r.statusCode);
			spdlog::trace("HTTP content-type: {}", r.headers["content-type"]);

			// parse json and print number of models
			auto j = nlohmann::json::parse(r.text());
			spdlog::debug("Total number of models: {}", j.size());

			// filter models by id ends with {HF_MODEL_ENDS_WITH}
			spdlog::trace("Filtering models by id ends with {}", HF_MODEL_ENDS_WITH);
			auto foundModels = nlohmann::json::array();
			for (auto &model : j) {
				auto id = model["id"].get<std::string>();
				if (id.ends_with(HF_MODEL_ENDS_WITH)) {
					foundModels.push_back(model);
				}
			}
			spdlog::trace("Total number of models ending with {}: {}", HF_MODEL_ENDS_WITH, foundModels.size());
			spdlog::trace("Total filtered number of models: {}", foundModels.size());

			// group models by lastModified (date only)
			spdlog::trace("Grouping models by lastModified (date only)");
			std::map<std::string, std::vector<nlohmann::json>> sortedModels;
			for (auto &model : foundModels) {
				auto lastModified = model["lastModified"].get<std::string>().substr(0, 10);
				sortedModels[lastModified].push_back(model);
			}

			// now that we have a map of models, we can sort each vector by likes
			spdlog::trace("Sorting grouped models by likes");
			for (auto &pair : sortedModels) {
				auto &val = pair.second;  // Assuming 'sortedModels' is a map or similar associative container
				std::sort(val.begin(), val.end(), [](const auto &a, const auto &b) {
					auto likesA = a["likes"].template get<int>();
					auto likesB = b["likes"].template get<int>();
					return likesA > likesB;
				});
			}

			spdlog::trace("Flattening sorted models");
			std::vector<nlohmann::json> modelsFlattened;
			for (auto &pair : sortedModels) {
				auto &models = pair.second; // Assuming sortedModels is a map or similar associative container
				for (auto &model : models) {
					modelsFlattened.push_back(model);
				}
			}

			// sort the flattened vector by lastModified descending
			spdlog::trace("Sorting flattened models by lastModified descending");

			std::sort(modelsFlattened.begin(), modelsFlattened.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
				std::string lastModifiedA = a["lastModified"].template get<std::string>();
				std::string lastModifiedB = b["lastModified"].template get<std::string>();
				return lastModifiedA > lastModifiedB;
			});

			spdlog::debug("Total number of models after filtering, grouping, and sorting: {}", modelsFlattened.size());
			return modelsFlattened;
		} catch (std::exception &e) {
			spdlog::error("Failed to get models: {}", e.what());
			return nlohmann::json::array();
		}
	}

	nlohmann::json ParseRawModels(const nlohmann::json &rawModels)
	{
		/* Example return value:
		[
			// without split model:
			{
				"downloads": 0,
				"hasSplitModel": false,
				"id": "TheBloke/Arithmo-Mistral-7B-GGUF",
				"lastModified": "2023-10-20T13:54:18.000Z",
				"likes": 4,
				"modelId": "Arithmo-Mistral-7B-GGUF",
				"modelName": "Arithmo-Mistral-7B",
				"name": "TheBloke/Arithmo-Mistral-7B",
				"quantizations": {
					"Q2_K": [
						"arithmo-mistral-7b.Q2_K.gguf"
					],
					"Q3_K_L": [
						"arithmo-mistral-7b.Q3_K_L.gguf"
					],
					"Q3_K_M": [
						"arithmo-mistral-7b.Q3_K_M.gguf"
					],
					"Q3_K_S": [
						"arithmo-mistral-7b.Q3_K_S.gguf"
					],
					"Q4_0": [
						"arithmo-mistral-7b.Q4_0.gguf"
					],
					"Q4_K_M": [
						"arithmo-mistral-7b.Q4_K_M.gguf"
					],
					"Q4_K_S": [
						"arithmo-mistral-7b.Q4_K_S.gguf"
					],
					"Q5_0": [
						"arithmo-mistral-7b.Q5_0.gguf"
					],
					"Q5_K_M": [
						"arithmo-mistral-7b.Q5_K_M.gguf"
					],
					"Q5_K_S": [
						"arithmo-mistral-7b.Q5_K_S.gguf"
					],
					"Q6_K": [
						"arithmo-mistral-7b.Q6_K.gguf"
					],
					"Q8_0": [
						"arithmo-mistral-7b.Q8_0.gguf"
					]
				},
				"repoUser": "TheBloke"
			},
			// with split model:
			{
				"downloads": 215,
				"hasSplitModel": true,
				"id": "TheBloke/Falcon-180B-Chat-GGUF",
				"lastModified": "2023-10-19T12:33:48.000Z",
				"likes": 105,
				"modelId": "Falcon-180B-Chat-GGUF",
				"modelName": "Falcon-180B-Chat",
				"name": "TheBloke/Falcon-180B-Chat",
				"quantizations": {
					"Q2_K": [
						"falcon-180b-chat.Q2_K.gguf-split-a",
						"falcon-180b-chat.Q2_K.gguf-split-b"
					],
					"Q3_K_L": [
						"falcon-180b-chat.Q3_K_L.gguf-split-a",
						"falcon-180b-chat.Q3_K_L.gguf-split-b"
					],
					"Q3_K_M": [
						"falcon-180b-chat.Q3_K_M.gguf-split-a",
						"falcon-180b-chat.Q3_K_M.gguf-split-b"
					],
					"Q3_K_S": [
						"falcon-180b-chat.Q3_K_S.gguf-split-a",
						"falcon-180b-chat.Q3_K_S.gguf-split-b"
					],
					"Q4_0": [
						"falcon-180b-chat.Q4_0.gguf-split-a",
						"falcon-180b-chat.Q4_0.gguf-split-b",
						"falcon-180b-chat.Q4_0.gguf-split-c"
					],
					"Q4_K_M": [
						"falcon-180b-chat.Q4_K_M.gguf-split-a",
						"falcon-180b-chat.Q4_K_M.gguf-split-b",
						"falcon-180b-chat.Q4_K_M.gguf-split-c"
					],
					"Q4_K_S": [
						"falcon-180b-chat.Q4_K_S.gguf-split-a",
						"falcon-180b-chat.Q4_K_S.gguf-split-b",
						"falcon-180b-chat.Q4_K_S.gguf-split-c"
					],
					"Q5_0": [
						"falcon-180b-chat.Q5_0.gguf-split-a",
						"falcon-180b-chat.Q5_0.gguf-split-b",
						"falcon-180b-chat.Q5_0.gguf-split-c"
					],
					"Q5_K_M": [
						"falcon-180b-chat.Q5_K_M.gguf-split-a",
						"falcon-180b-chat.Q5_K_M.gguf-split-b",
						"falcon-180b-chat.Q5_K_M.gguf-split-c"
					],
					"Q5_K_S": [
						"falcon-180b-chat.Q5_K_S.gguf-split-a",
						"falcon-180b-chat.Q5_K_S.gguf-split-b",
						"falcon-180b-chat.Q5_K_S.gguf-split-c"
					],
					"Q6_K": [
						"falcon-180b-chat.Q6_K.gguf-split-a",
						"falcon-180b-chat.Q6_K.gguf-split-b",
						"falcon-180b-chat.Q6_K.gguf-split-c"
					],
					"Q8_0": [
						"falcon-180b-chat.Q8_0.gguf-split-a",
						"falcon-180b-chat.Q8_0.gguf-split-b",
						"falcon-180b-chat.Q8_0.gguf-split-c",
						"falcon-180b-chat.Q8_0.gguf-split-d"
					]
				},
				"repoUser": "TheBloke"
			},
		]
		*/
		spdlog::trace("Total number of rawModels: {}", rawModels.size());

		auto json = nlohmann::json::array();

		for (auto model : rawModels) {
			nlohmann::json j;
			const auto id = model["id"].get<std::string>();
			j["id"] = id;
			j["name"] = StripFormatFromModelRepo(id);
			j["createdAt"] = model["createdAt"].get<std::string>();
			j["lastModified"] = model["lastModified"].get<std::string>();
			j["likes"] = model["likes"].get<int>();
			j["downloads"] = model["downloads"].get<int>();
			// id is composed of two parts in the format `modelRepo/modelId`
			const auto parts = util::splitString(id, '/');
			j["repoUser"] = parts[0];
			const auto &modelId = parts[1];
			j["modelId"] = modelId;
			j["modelName"] = StripFormatFromModelRepo(modelId);
			std::map<std::string, std::vector<nlohmann::json>> quantizations;
			for (auto &sibling : model["siblings"]) {
				const auto name = sibling["rfilename"].get<std::string>();
				const auto isSplitModel = util::stringContains(name, "gguf-split");
				const auto isFullModel = name.ends_with(HF_MODEL_FILE_EXTENSION);
				if (isFullModel || isSplitModel) {
					// quantization is the next to last part of the filename
					const auto &p = util::splitString(name, '.');
					const auto &quantization = p[p.size() - 2];
					quantizations[quantization].emplace_back(name);
				}
				j["hasSplitModel"] = isSplitModel;
			}
			j["quantizations"] = quantizations;
			json.push_back(j);
		}
		return json;
	}

	nlohmann::json GetModels()
	{
		return ParseRawModels(GetRawModels());
	}

	nlohmann::json GetAIModels(orm::ItemActionsFactory &actionsFactory)
	{
		// std::regex sizePhiRegex(R"(phi\-\d\.?\d)");
		std::regex sizePhi1Regex(R"(phi-1)");
		std::regex sizePhi2Regex(R"(phi-2)");
		std::regex sizeMoeRegex(R"(\d+x\d+\.?\d*(K|k|M|m|B|b|T|t|Q|q))");
		std::regex sizeRegex(R"(\d+\.?\d*(K|k|M|m|B|b|T|t|Q|q))");
		std::vector<AIModel> aiModels;
		const auto models = GetModels();
		const auto modelIQData = FetchAndParseModelIQData();
		const auto modelEQData = FetchAndParseModelEQData();
		// Separate the scores for statistical calculations
		std::vector<double> eqBenchScores;
		std::vector<double> magiScores;

		for (const auto &eqRow: modelEQData.value()) {
			eqBenchScores.push_back(eqRow.second.first.score);
			magiScores.push_back(eqRow.second.second.score);
		}

		double meanEqBench = CalculateMean(eqBenchScores);
		double meanMagi = CalculateMean(magiScores);
		double correlation = CalculateCorrelation(eqBenchScores, magiScores);
		const auto downloadedModelNamesOnDisk = orm::DownloadItemActions::getDownloadItemNames(actionsFactory.download());
		// get list of models on disk with inference status of "error"
		const auto wingmanItems = actionsFactory.wingman()->getAll();
		std::vector<WingmanItem> wingmanItemsWithErrors;
		for (auto &item : wingmanItems) {
			if (item.status == WingmanItemStatus::error) {
				wingmanItemsWithErrors.push_back(item);
			}
		}

		auto itDefault = std::find_if(downloadedModelNamesOnDisk.begin(), downloadedModelNamesOnDisk.end(),
							  [](const DownloadItemName &si) {
			return util::stringCompare(si.modelRepo, "default", false) &&
				util::stringCompare(si.filePath, "default.gguf", false);
		});

		if (itDefault != downloadedModelNamesOnDisk.end()) {
			AIModel aiModel;
			aiModel.id = "default";
			aiModel.name = "Default Model";
			aiModel.vendor = "meta";
			aiModel.location = fmt::format("{}/{}", HF_MODEL_URL, "default");
			aiModel.maxLength = DEFAULT_CONTEXT_LENGTH;
			aiModel.tokenLimit = DEFAULT_CONTEXT_LENGTH * 16;
			aiModel.downloads = -1;
			aiModel.likes = -1;
			DownloadableItem di;
			di.modelRepo = "default";
			di.modelRepoName = "Default Model Repo";
			di.filePath = "default.gguf";
			di.quantization = "QD";
			di.quantizationName = "Default";
			di.location = "";
			di.available = true;
			di.isDownloaded = true;
			di.hasError = false;
			di.downloads = -1;
			di.likes = -1;
			aiModel.items.push_back(di);
			aiModels.push_back(aiModel);
		}
	   // first check if models is empty. if so, return the downloaded models only
		if (models.empty()) {
			for (auto &model : downloadedModelNamesOnDisk) {
				AIModel aiModel;
				aiModel.id = model.modelRepo;
				aiModel.name = StripFormatFromModelRepo(model.modelRepo);
				aiModel.vendor = "meta";
				aiModel.location = fmt::format("{}/{}", HF_MODEL_URL, model.modelRepo);
				aiModel.maxLength = DEFAULT_CONTEXT_LENGTH;
				aiModel.tokenLimit = DEFAULT_CONTEXT_LENGTH * 16;
				aiModel.downloads = -1;
				aiModel.likes = -1;
				std::smatch match;
				if (std::regex_search(aiModel.name, match, sizeMoeRegex)) {
					std::string modifiedMatch = match[0].str(); // Convert to string
					modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
					aiModel.size = modifiedMatch; // Assign modified string
				} else if (std::regex_search(aiModel.name, match, sizeRegex)) {
					std::string modifiedMatch = match[0].str(); // Convert to string
					modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
					aiModel.size = modifiedMatch; // Assign modified string
				} else if (std::regex_search(aiModel.name, match, sizePhi1Regex)) {
					aiModel.size = "1.3B";
				} else if (std::regex_search(aiModel.name, match, sizePhi2Regex)) {
					aiModel.size = "2.7B";
				}
				auto mIQ = GetModelIQData(aiModel.id, modelIQData);
				if (mIQ.has_value()) {
					auto modelEval = mIQ.value();
					aiModel.iQScore = CalculateModelIQScore(modelEval);
					if (modelEval.paramsBillion > 0.0) {
						aiModel.size = fmt::format("{:.1f}B", modelEval.paramsBillion);
					}
				}
				else
					aiModel.iQScore = -1.0;

				auto mEQ = modelEQData.value();
				if (mEQ.contains(util::stringLower(aiModel.id))) {
					std::pair<EqBenchData, MagiData>& modelEQ = mEQ[aiModel.id];
					aiModel.eQScore = CalculateCombinedEQScore({ aiModel.name, modelEQ.first.score, modelEQ.second.score }, meanEqBench, meanMagi, correlation);
				}
				else {
					aiModel.eQScore = -1.0;
				}
			for (auto &item : downloadedModelNamesOnDisk) {
					if (util::stringCompare(item.modelRepo, aiModel.id, false)) {
						DownloadableItem di;
						di.modelRepo = item.modelRepo;
						di.modelRepoName = StripFormatFromModelRepo(item.modelRepo);
						di.filePath = item.filePath;
						di.quantization = item.quantization;
						di.quantizationName = util::quantizationNameFromQuantization(di.quantization);
						di.location = orm::DownloadItemActions::urlForModel(di.modelRepo, di.filePath);
						di.available = true;
						di.isDownloaded = true;
						di.downloads = -1;
						di.likes = -1;
						// const auto it = std::ranges::find_if(wingmanItemsWithErrors, [di](const auto &wi) {
						// 	return util::stringCompare(wi.modelRepo, di.modelRepo, false) &&
						// 		util::stringCompare(wi.filePath, di.filePath, false);
						// });
						auto it = std::find_if(wingmanItemsWithErrors.begin(), wingmanItemsWithErrors.end(),
											[di](const WingmanItem &wi) {
							return util::stringCompare(wi.modelRepo, di.modelRepo, false) &&
								util::stringCompare(wi.filePath, di.filePath, false);
						});
						di.hasError = it != wingmanItemsWithErrors.end() ? true : false;
						aiModel.items.push_back(di);
					}
				}
				aiModels.push_back(aiModel);
			}
			return aiModels;
		}
		int index = 0;
		for (auto &model : models) {
			const auto &id = model["id"].get<std::string>();
			const auto &name = model["name"].get<std::string>();
			const auto &hasSplitModel = model["hasSplitModel"].get<bool>();

			if (hasSplitModel)
				continue; // split models are not supported yet (TODO: support split models)
			const auto &quantizations = model["quantizations"];
			AIModel aiModel;
			aiModel.id = id;
			aiModel.name = name;
			aiModel.vendor = "meta";
			aiModel.location = fmt::format("{}/{}", HF_MODEL_URL, id);
			aiModel.maxLength = DEFAULT_CONTEXT_LENGTH;
			aiModel.tokenLimit = DEFAULT_CONTEXT_LENGTH * 16;
			aiModel.downloads = model["downloads"].get<int>();
			aiModel.likes = model["likes"].get<int>();
			aiModel.updated = model["lastModified"].get<std::string>();
			aiModel.created = model["createdAt"].get<std::string>();
			std::smatch match;
			// if (aiModel.name.find("phi-2") != std::string::npos) {
			// 	aiModel.size = "2.8B";
			// } else if (aiModel.name.find("phi-1") != std::string::npos) {
			// 	aiModel.size = "1.3B";
			// }
			if (std::regex_search(aiModel.name, match, sizeMoeRegex)) {
				std::string modifiedMatch = match[0].str(); // Convert to string
				modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
				aiModel.size = modifiedMatch; // Assign modified string
			} else if (std::regex_search(aiModel.name, match, sizeRegex)) {
				std::string modifiedMatch = match[0].str(); // Convert to string
				modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
				aiModel.size = modifiedMatch; // Assign modified string
			} else if (std::regex_search(aiModel.name, match, sizePhi1Regex)) {
				aiModel.size = "1.3B";
			} else if (std::regex_search(aiModel.name, match, sizePhi2Regex)) {
				aiModel.size = "2.8B";
			}
			auto lowerName = util::stringLower(aiModel.name);
			auto mIQData = GetModelIQData(lowerName, modelIQData);
			if (mIQData.has_value()) {
				auto modelEval = mIQData.value();
				aiModel.iQScore = CalculateModelIQScore(modelEval);
				if (modelEval.paramsBillion > 0.0) {
					aiModel.size = fmt::format("{:.1f}B", modelEval.paramsBillion);
				}
			} else
				aiModel.iQScore = -1.0;

			auto mEQData = modelEQData.value();
			// if (mEQData.contains(lowerName)) {
			// 	auto &[eqbData, magiData] = mEQData[lowerName];
			// 	aiModel.eQScore = CalculateCombinedEQScore(
			// 		{ lowerName, eqbData.score, magiData.score },
			// 		meanEqBench, meanMagi, correlation);
			if (modelEQData.has_value()) {
				// auto eqmData = GetModelEQData(lowerName, modelEQData);
				auto eqmData = GetModelEQData(lowerName, modelEQData.value());
				if (eqmData.has_value()) {
					auto &[eqbData, magiData] = eqmData.value();
					aiModel.eQScore = CalculateCombinedEQScore(
						{ lowerName, eqbData.score, magiData.score },
						meanEqBench, meanMagi, correlation);
				} else {
					aiModel.eQScore = -1.0;
				}
			} else
				aiModel.eQScore = -1.0;

			std::vector<DownloadableItem> items;
			for (auto &[key, value] : quantizations.items()) {
				DownloadableItem item;
				item.modelRepo = id;
				item.modelRepoName = name;
				item.filePath = value.front().get<std::string>();
				item.quantization = key;
				std::string quantizationName;
				item.quantizationName = util::quantizationNameFromQuantization(item.quantization);
				item.location = orm::DownloadItemActions::urlForModel(item.modelRepo, item.filePath);
				item.available = true;
				item.downloads = -1;
				item.likes = -1;

				// set item.isDownloaded by searching modelNamesOnDisk for matching, case-insensitive, modelRepo and filePath
				// const auto it = std::ranges::find_if(downloadedModelNamesOnDisk, [item](const auto &si) {
				// 	return util::stringCompare(si.modelRepo, item.modelRepo, false) &&
				// 		util::stringCompare(si.filePath, item.filePath, false);
				// });
				auto it = std::find_if(downloadedModelNamesOnDisk.begin(), downloadedModelNamesOnDisk.end(),
									[item](const DownloadItemName &si) {
					return util::stringCompare(si.modelRepo, item.modelRepo, false) &&
						util::stringCompare(si.filePath, item.filePath, false);
				});
				item.isDownloaded = it != downloadedModelNamesOnDisk.end() ? true : false;
				// const auto itError = std::ranges::find_if(wingmanItemsWithErrors, [item](const auto &wi) {
				// 	return util::stringCompare(wi.modelRepo, item.modelRepo, false) &&
				// 		util::stringCompare(wi.filePath, item.filePath, false);
				// });
				auto itError = std::find_if(wingmanItemsWithErrors.begin(), wingmanItemsWithErrors.end(),
											[item](const WingmanItem &wi) {
					return util::stringCompare(wi.modelRepo, item.modelRepo, false) &&
						util::stringCompare(wi.filePath, item.filePath, false);
				});
				item.hasError = itError != wingmanItemsWithErrors.end() ? true : false;
				items.push_back(item);
			}
			// TODO: check if there are any downloaded models that no longer exist on the server, e.g., if its on disk, but not in the quantizations list

			aiModel.items = items;
			aiModels.push_back(aiModel);
			index++;
		}
		return aiModels;
	}

	bool HasAIModel(const std::string &modelRepo, const std::string &filePath)
	{
		const auto models = GetModels();
		for (auto &model : models) {
			const auto id = model["id"].get<std::string>();
			if (util::stringCompare(id, modelRepo, false)) {
				for (auto &[key, value] : model["quantizations"].items()) {
					for (auto &file : value) {
						if (util::stringCompare(file, filePath, false)) {
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	nlohmann::json FilterModels(nlohmann::json::const_reference models, const std::string &modelRepo, const std::optional<std::string> &filename, const std::optional<std::string> &quantization)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}
		if (!filename && !quantization) {
			throw std::runtime_error("either filename or quantization is required, but both are empty");
		}
		if (filename && quantization) {
			throw std::runtime_error("either filename or quantization is required, but both are provided");
		}

		const bool byFilePath = static_cast<bool>(filename);
		const bool byQuantization = static_cast<bool>(quantization);
		auto filteredModels = nlohmann::json::array();

		for (auto &model : models) {
			const auto id = model["id"].get<std::string>();
			if (util::stringCompare(id, modelRepo, false)) {
				for (auto &[key, value] : model["quantizations"].items()) {
					if (byQuantization && util::stringCompare(key, quantization.value(), false)) {
						filteredModels.push_back(model);
						// quantization found so no need to continue
						break;
					}
					if (byFilePath) {
						for (auto &file : value) {
							if (util::stringCompare(file, filename.value(), false)) {
								filteredModels.push_back(model);
							}
						}
					}
				}
			}
		}
		return filteredModels;
	}

	nlohmann::json GetModelByFilename(const std::string &modelRepo, std::string filename)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}
		if (filename.empty()) {
			throw std::runtime_error("filename is required, but is empty");
		}

		return FilterModels(GetModels(), modelRepo, filename);
	}

	std::optional<nlohmann::json> GetModelByQuantization(const std::string &modelRepo, std::string quantization)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}
		if (quantization.empty()) {
			throw std::runtime_error("quantization is required, but is empty");
		}

		const auto models = FilterModels(GetModels(), modelRepo, {}, quantization);

		if (models.empty()) {
			return std::nullopt;
		}
		return models[0];
	}

	// filter a list of models that have a particular quantization
	nlohmann::json FilterModelsByQuantization(nlohmann::json::const_reference models, const std::string &quantization)
	{
		if (quantization.empty()) {
			throw std::runtime_error("quantization is required, but is empty");
		}

		auto filteredModels = nlohmann::json::array();

		for (auto &model : models) {
			for (auto &[key, value] : model["quantizations"].items()) {
				if (util::stringCompare(key, quantization, false)) {
					filteredModels.push_back(model);
				}
			}
		}
		return filteredModels;
	}

	nlohmann::json GetModelsByQuantization(const std::string &quantization)
	{
		if (quantization.empty()) {
			throw std::runtime_error("quantization is required, but is empty");
		}

		return FilterModelsByQuantization(GetModels(), quantization);
	}

	nlohmann::json GetModelQuantizations(const std::string &modelRepo)
	{
		if (modelRepo.empty()) {
			throw std::runtime_error("modelRepo is required, but is empty");
		}

		auto filteredModels = FilterModels(GetModels(), modelRepo);
		auto quantizations = nlohmann::json::array();
		for (auto &model : filteredModels) {
			for (auto &item : model["quantizations"].items()) {
				quantizations.push_back(item);
			}
		}
		// // remove duplicates
		// quantizations.erase(std::ranges::unique(quantizations).begin(), quantizations.end());
		// Sort the container first to bring duplicates together
		std::sort(quantizations.begin(), quantizations.end());

		// std::unique reorders the elements so that each unique element appears at the beginning,
		// and returns an iterator to the new end of the unique range.
		auto last = std::unique(quantizations.begin(), quantizations.end());

		// Erase the non-unique elements.
		quantizations.erase(last, quantizations.end());
		return quantizations;
	}

}
