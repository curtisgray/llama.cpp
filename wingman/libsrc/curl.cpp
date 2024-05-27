#include <string>
#include <fstream>
#include <curl/curl.h>
#include <rapidcsv.h>

#include "json.hpp"
#include "types.h"
#include "curl.h"

#include <set>

#include "hwinfo.h"
#include "orm.h"
#include "util.hpp"
#include "parse_evals.h"
#include "inferable.h"

namespace wingman::curl {
	std::string wingman::curl::GetHFModelListUrl(int limit)
	{
		return fmt::format("{}&limit={}", HF_ALL_MODELS_URL_BASE, limit);
	}

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
		if (util::stringEndsWith(modelRepo,HF_MODEL_ENDS_WITH, false)) {
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
		[[maybe_unused]] CURLcode res;

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
				spdlog::trace("Requesting url: {}", request.url);
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
			spdlog::trace("Response code: {}", response.statusCode);
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

#pragma region EQ data
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

	void ParseModelIQData(std::string csvData, std::vector<ModelIQEval> &models)
	{
		// Use rapidcsv to parse the CSV data
		std::stringstream csvStream(csvData);
		// rapidcsv::Document doc(csvStream, rapidcsv::LabelParams(-1, -1));
		rapidcsv::ConverterParams converterParams(true, -1.0, -1); // set all missing values to -1
		rapidcsv::Document doc(csvStream, rapidcsv::LabelParams(), rapidcsv::SeparatorParams(), converterParams);

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
	}

	std::string GetIQEQAssetPath()
	{
		fs::path pathToAssets;

#if NDEBUG
		pathToAssets = fs::path(EQ_MODEL_DATA_PATH_PROD);
#else
		pathToAssets = fs::path(EQ_MODEL_DATA_PATH_DEV);
#endif

		// Resolve the relative path against the current working directory and normalize
		const fs::path cwd = fs::current_path();
		const fs::path fullPath = cwd / pathToAssets;
		std::string ret = fs::canonical(fullPath).string(); // Converts to an absolute path, resolving any '..'
		spdlog::debug("Asset path: {}", ret);
		return ret;
	}

	std::string FetchAsset(const std::string &assetPath)
	{
		const std::string pathToAssets = GetIQEQAssetPath();

		// Using std::filesystem for path construction
		const std::filesystem::path filePath = std::filesystem::path(pathToAssets) / assetPath;

		std::vector<ModelIQEval> models;

		std::ifstream file(filePath);
		if (!file) {
			std::cerr << "Failed to open file at " << filePath << std::endl;
			return ""; // Return empty string if file can't be opened
		}

		return std::string((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
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

		std::vector<ModelIQEval> models;
		ParseModelIQData(csvData, models);

		return models;
	}

	std::vector<ModelIQEval> FetchAndParseModelIQDataLocal()
	{
		std::vector<ModelIQEval> models;
		const std::string input = FetchAsset(HF_MODEL_LEADERBOARD_CSV_URL);
		ParseModelIQData(input, models);
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

	std::optional< std::map<std::string, std::pair<EqBenchData, MagiData>> > FetchAndParseModelEQDataLocal()
	{
		try {
			const std::string input = FetchAsset(EQ_MODEL_DATA_URL);
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
#pragma endregion

	std::string GetModelSize(AIModel& aiModel)
	{
		std::string size = "";
		std::regex sizePhi1Regex(R"(phi-1)", std::regex_constants::icase);
		std::regex sizePhi2Regex(R"(phi-2)", std::regex_constants::icase);
		std::regex sizePhi3Regex(R"(phi-3)", std::regex_constants::icase);
		std::regex sizeOpenChatRegex(R"(openchat)", std::regex_constants::icase);
		std::regex sizeGarrulusRegex(R"(garrulus)", std::regex_constants::icase);
		std::regex sizeMedicineRegex(R"(medicine)", std::regex_constants::icase);
		std::regex sizeMoeRegex(R"(\d+x\d+\.?\d*(m|b|t|q))", std::regex_constants::icase);
		std::regex sizeRegex(R"(\d+\.?\d*(m|b|t|q))", std::regex_constants::icase);

		std::smatch match;
		if (std::regex_search(aiModel.name, match, sizeMoeRegex)) {
			std::string modifiedMatch = match[0].str(); // Convert to string
			modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
			size = modifiedMatch; // Assign modified string
		} else if (std::regex_search(aiModel.name, match, sizeRegex)) {
			std::string modifiedMatch = match[0].str(); // Convert to string
			modifiedMatch.back() = std::toupper(modifiedMatch.back()); // Convert last char to uppercase
			size = modifiedMatch; // Assign modified string
		} else if (std::regex_search(aiModel.name, match, sizePhi1Regex)) {
			size = "1.3B";
		} else if (std::regex_search(aiModel.name, match, sizePhi2Regex)) {
			size = "2.8B";
		} else if (std::regex_search(aiModel.name, match, sizePhi3Regex)) {
			size = "3.8B";
		} else if (std::regex_search(aiModel.name, match, sizeOpenChatRegex)) {
			size = "7B";
		} else if (std::regex_search(aiModel.name, match, sizeGarrulusRegex)) {
			size = "7B";
		} else if (std::regex_search(aiModel.name, match, sizeMedicineRegex)) {
			size = "7B";
		} else {
			size = "8B";
		}
		return size;
	}

	void SetModelScores(AIModel& aiModel,
		const std::vector<ModelIQEval> &modelIQData,
		const std::optional< std::map<std::string, std::pair<EqBenchData, MagiData>> > &modelEQData,
		double meanEqBench, double meanMagi, double correlation)
	{
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
	}

	nlohmann::json GetRawModels(int maxModelsToRetrieve)
	{
		try {
			const auto startTime = std::chrono::high_resolution_clock::now();
			spdlog::trace("Fetching models from {}", GetHFModelListUrl(maxModelsToRetrieve));
			auto r = Fetch(GetHFModelListUrl(maxModelsToRetrieve));
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
			spdlog::debug("Time taken to fetch raw models: {} ms", duration);
			spdlog::trace("HTTP status code: {}", r.statusCode);
			spdlog::trace("HTTP content-type: {}", r.headers["content-type"]);

			// parse json and print number of models
			auto j = nlohmann::json::parse(r.text());
			spdlog::debug("Total number of models fetched: {}", j.size());

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
			spdlog::trace("Total number of models after filtering: {}", foundModels.size());

			spdlog::debug("Total number of models after filtering: {}", foundModels.size());
			duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
			spdlog::debug("Time taken to fetch and filter raw models: {} ms", duration);
			return foundModels;
		} catch (std::exception &e) {
			spdlog::error("Failed to get models: {}", e.what());
			return nlohmann::json::array();
		}
	}

	nlohmann::json ParseRawModels(const nlohmann::json &rawModels)
	{
		const auto startTime = std::chrono::high_resolution_clock::now();
		spdlog::trace("Total number of raw models: {}", rawModels.size());

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
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
		spdlog::debug("Time taken to parse raw models: {} ms", duration);
		return json;
	}

	nlohmann::json GetModels(int maxModelsToRetrieve)
	{
		const auto startTime = std::chrono::high_resolution_clock::now();
		auto models = ParseRawModels(GetRawModels(maxModelsToRetrieve));
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
		spdlog::debug("Total time taken to get and parse raw models into AI models: {} ms", duration);
		return models;
	}

	
	nlohmann::json GetAIModelsFast(orm::ItemActionsFactory &actionsFactory, int maxModelsToRetrieve)
	{
		try {
			const auto startTime = std::chrono::high_resolution_clock::now();
			const HardwareInfo hardwareInfo = GetHardwareInfo();
			std::vector<AIModel> aiModels;

			// Fetch raw model data 
			spdlog::trace("Fetching models from {}", GetHFModelListUrl(maxModelsToRetrieve));
			auto response = Fetch(GetHFModelListUrl(maxModelsToRetrieve));
			if (response.curlCode != CURLE_OK || response.statusCode != 200) {
				throw std::runtime_error("Failed to fetch models");
			}

			// Parse the JSON 
			auto models = nlohmann::json::parse(response.text());
			spdlog::debug("Total number of raw models fetched: {}", models.size());

			// Get model details and error information
			const auto downloadedModelNamesOnDisk = orm::DownloadItemActions::getDownloadItemNames(actionsFactory.download());
			const auto wingmanItems = actionsFactory.wingman()->getAll();
			std::vector<WingmanItem> wingmanItemsWithErrors;
			for (auto &item : wingmanItems) {
				if (item.status == WingmanItemStatus::error) {
					wingmanItemsWithErrors.push_back(item);
				}
			}

			// Create a set of fetched model IDs for quick lookups
			std::set<std::string> fetchedModelIds;
			for (const auto &model : models) {
				fetchedModelIds.insert(model["id"].get<std::string>());
			}

			// Lambda to create and populate an AIModel
			auto createAIModel = [&](const nlohmann::json &modelJson, const std::string &id, const std::string &name, bool available) -> AIModel {
				AIModel aiModel;
				aiModel.id = id;
				aiModel.name = name;
				aiModel.vendor = "meta";
				// aiModel.location = available ? fmt::format("{}/{}", HF_MODEL_URL, id) : "not available";
				aiModel.location = fmt::format("{}/{}", HF_MODEL_URL, id);
				aiModel.maxLength = DEFAULT_CONTEXT_LENGTH;
				aiModel.tokenLimit = DEFAULT_CONTEXT_LENGTH * 16;
				aiModel.downloads = available ? modelJson["downloads"].get<int>() : 0;
				aiModel.likes = available ? modelJson["likes"].get<int>() : 0;
				aiModel.updated = !modelJson["lastModified"].empty() ? modelJson["lastModified"].get<std::string>() : "unknown";
				aiModel.created = !modelJson["createdAt"].empty() ? modelJson["createdAt"].get<std::string>() : "unknown";
				aiModel.size = GetModelSize(aiModel);
				aiModel.iQScore = -1.0;
				aiModel.eQScore = -1.0;
				return aiModel;
			};

			// Lambda to create and populate a DownloadableItem
			auto createDownloadableItem = [&](const AIModel &aiModel, const std::string &filePath, const std::string &quantization, const bool available) -> DownloadableItem {
				DownloadableItem di;
				di.modelRepo = aiModel.id;
				di.modelRepoName = aiModel.name;
				di.filePath = filePath;
				di.quantization = quantization;
				di.quantizationName = util::quantizationNameFromQuantization(di.quantization);
				di.location = orm::DownloadItemActions::urlForModel(di.modelRepo, di.filePath);
				di.available = available;
				di.isDownloaded = std::find_if(downloadedModelNamesOnDisk.begin(), downloadedModelNamesOnDisk.end(),
					[di](const DownloadItemName &si) {
					return util::stringCompare(si.modelRepo, di.modelRepo, false) &&
						util::stringCompare(si.filePath, di.filePath, false);
				}) != downloadedModelNamesOnDisk.end();
				di.hasError = std::find_if(wingmanItemsWithErrors.begin(), wingmanItemsWithErrors.end(),
					[di](const WingmanItem &wi) {
					return util::stringCompare(wi.modelRepo, di.modelRepo, false) &&
						util::stringCompare(wi.filePath, di.filePath, false);
				}) != wingmanItemsWithErrors.end();
				const auto inferability = CheckInferability(aiModel, hardwareInfo, util::quantizationToBits(di.quantization));
				di.isInferable = inferability.isInferable;
				di.normalizedQuantizedMemRequired = inferability.normalizedQuantizedMemRequired;
				return di;
			};

			// Process fetched models
			std::regex splitRegex(R"(\-\d+\-OF\-\d+)", std::regex_constants::icase);
			for (auto &model : models) {
				const auto &id = model["id"].get<std::string>();
				if (!util::stringEndsWith(id, HF_MODEL_ENDS_WITH, false)) {
					continue;
				}
				const auto &name = StripFormatFromModelRepo(id);

				// Check for split models
				bool isSplitModel = false;
				for (auto &sibling : model["siblings"]) {
					const auto n = sibling["rfilename"].get<std::string>();
					isSplitModel = util::stringContains(n, "split", false) || std::regex_search(n, splitRegex);
					if (isSplitModel) {
						break;
					}
				}
				if (isSplitModel) {
					continue;
				}

				// Extract quantizations
				std::map<std::string, std::vector<nlohmann::json>> quantizations;
				for (auto &sibling : model["siblings"]) {
					const auto n = sibling["rfilename"].get<std::string>();
					if (util::stringEndsWith(n, HF_MODEL_FILE_EXTENSION, false)) {
						const auto quantization = util::extractQuantizationFromFilename(n);
						if (!quantization.empty()) {
							quantizations[quantization].emplace_back(n);
						} else {
							spdlog::warn("Failed to extract quantization from filename: {}", n);
						}
					}
				}
				if (quantizations.empty()) {
					continue;
				}

				// Create AIModel for the fetched model
				AIModel aiModel = createAIModel(model, id, name, true);

				// Create DownloadableItems for each quantization
				for (auto &[key, value] : quantizations) {
					DownloadableItem di = createDownloadableItem(aiModel, value.front().get<std::string>(), key, true);
					if (di.isInferable) {
						aiModel.isInferable = true;
					}
					aiModel.items.push_back(di);
				}

				// Set default inferability
				const auto defaultInferability = CheckInferability(aiModel, hardwareInfo, util::quantizationToBits("FP16"));
				aiModel.totalMemory = defaultInferability.totalMemory;
				aiModel.availableMemory = defaultInferability.availableMemory;
				aiModel.normalizedQuantizedMemRequired = defaultInferability.normalizedQuantizedMemRequired;

				aiModels.push_back(aiModel);
			}

			// Add downloaded models not present in the fetched list
			for (const auto &downloadedModel : downloadedModelNamesOnDisk) {
				if (fetchedModelIds.find(downloadedModel.modelRepo) == fetchedModelIds.end()) {
					// Model is downloaded but not in the fetched list
					nlohmann::json j;
					const auto dli = actionsFactory.download()->get(downloadedModel.modelRepo, downloadedModel.filePath);
					if (!dli.has_value()) {
						spdlog::warn("Failed to get downloaded item: {} {}", downloadedModel.modelRepo, downloadedModel.filePath);
						continue;
					}
					// set createdAt and lastModified to the string of the epoch time
					j["createdAt"] = util::toUniversalTimeString(dli->created);
					j["lastModified"] = util::toUniversalTimeString(dli->updated);
					AIModel aiModel = createAIModel(j, downloadedModel.modelRepo, StripFormatFromModelRepo(downloadedModel.modelRepo), false); // Pass empty json object 
					const auto quantization = util::extractQuantizationFromFilename(downloadedModel.filePath);
					DownloadableItem di = createDownloadableItem(aiModel, downloadedModel.filePath, quantization, false);
					di.available = false; // Mark as not available
					aiModel.items.push_back(di);
					// Set default inferability
					const auto defaultInferability = CheckInferability(aiModel, hardwareInfo, util::quantizationToBits(quantization));
					aiModel.totalMemory = defaultInferability.totalMemory;
					aiModel.availableMemory = defaultInferability.availableMemory;
					aiModel.normalizedQuantizedMemRequired = defaultInferability.normalizedQuantizedMemRequired;
					aiModels.push_back(aiModel);
				}
			}

			spdlog::debug("Total number of AI models accepted: {}", aiModels.size());

			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
			spdlog::debug("Time taken to fetch and process models: {} ms", duration);

			return aiModels;

		} catch (std::exception &e) {
			spdlog::error("Failed to fetch and process models efficiently: {}", e.what());
			return nlohmann::json::array();
		}
	}
}
