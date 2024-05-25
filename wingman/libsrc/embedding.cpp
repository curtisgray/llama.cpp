#include <vector>
#include <string>

#include "embedding.h"
#include "ingest.h"
#include "curl.h"
#include "orm.h"
#include "owned_cstrings.h"

namespace wingman::silk::embedding {
	orm::ItemActionsFactory actions_factory;
	WingmanItemStatus inference_status;

	const char * EmbeddingDb::getCreateEmbeddingTableSql() {
		return "CREATE TABLE IF NOT EXISTS embeddings ("
			"id INTEGER PRIMARY KEY, "
			"chunk TEXT, "
			"embedding BLOB, "
			"source TEXT, "
			"created INTEGER DEFAULT (unixepoch('now')) NOT NULL)";
	}

	sqlite3 * EmbeddingDb::openEmbeddingDatabase(const std::string &dbPath) {
		sqlite3 *ret = nullptr;
		char *errMsg = nullptr;

		int rc = sqlite3_open(dbPath.c_str(), &ret);
		if (rc) {
			// std::cerr << "Can't open database: " << sqlite3_errmsg(ret) << std::endl;
			spdlog::error("Can't open database: {}", sqlite3_errmsg(ret));
			return nullptr;
		}

		const auto createTableSql = getCreateEmbeddingTableSql();
		rc = sqlite3_exec(ret, createTableSql, nullptr, nullptr, &errMsg);
		if (rc != SQLITE_OK) {
			// std::cerr << "SQL error: " << errMsg << std::endl;
			spdlog::error("SQL error: {}", errMsg);
			sqlite3_free(errMsg);
			sqlite3_close(ret);
			return nullptr;
		}
		return ret;
	}

	void EmbeddingDb::closeEmbeddingDatabase(sqlite3 *db) {
		sqlite3_close(db);
	}

	EmbeddingDb::EmbeddingDb(std::string dbPath): dbPath_(std::move(dbPath)) {
		db_ = openEmbeddingDatabase(dbPath_);
		if (!db_) {
			throw std::runtime_error("Failed to open embedding database: " + dbPath_);
		}
	}

	EmbeddingDb::~EmbeddingDb() {
		closeEmbeddingDatabase(db_);
	}

	size_t EmbeddingDb::insertEmbeddingToDb(
		const std::string &chunk, const std::string &source, const std::vector<float> &embedding) const {
		sqlite3_stmt *stmt = nullptr;
		constexpr size_t id = -1;

		const std::string insertSql = "INSERT INTO embeddings (chunk, source, embedding) VALUES (?, ?, ?)";

		auto rc = sqlite3_prepare_v2(db_, insertSql.c_str(), -1, &stmt, nullptr);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to prepare statement: {}", sqlite3_errmsg(db_));
			return id;
		}

		// Bind the text chunk
		rc = sqlite3_bind_text(stmt, 1, chunk.c_str(), -1, SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to bind text chunk: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to bind text chunk: {}", sqlite3_errmsg(db_));
			return id;
		}

		// Bind the source
		rc = sqlite3_bind_text(stmt, 2, source.c_str(), -1, SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to bind source: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to bind source: {}", sqlite3_errmsg(db_));
			return id;
		}

		// Bind the embedding
		rc = sqlite3_bind_blob(stmt, 3, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to bind embedding: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to bind embedding: {}", sqlite3_errmsg(db_));
			return id;
		}

		// Execute the statement
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			sqlite3_reset(stmt);
			// std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to execute statement: {}", sqlite3_errmsg(db_));
			return id;
		}

		sqlite3_finalize(stmt);

		return static_cast<size_t>(sqlite3_last_insert_rowid(db_));
	}

	std::optional<EmbeddingRecord> EmbeddingDb::getEmbeddingById(const int id) const {
		sqlite3_stmt *stmt = nullptr;
		EmbeddingRecord record;

		const std::string selectSql = "SELECT id, chunk, embedding, source, created FROM embeddings WHERE id = ?";

		auto rc = sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &stmt, nullptr);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to prepare statement: {}", sqlite3_errmsg(db_));
			return std::nullopt;
		}

		// Bind the ID
		rc = sqlite3_bind_int(stmt, 1, id);
		if (rc != SQLITE_OK) {
			// std::cerr << "Failed to bind ID: " << sqlite3_errmsg(db_) << std::endl;
			spdlog::error("Failed to bind ID: {}", sqlite3_errmsg(db_));
			return std::nullopt;
		}

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			record.id = sqlite3_column_int(stmt, 0);
			record.chunk = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));

			const auto embeddingBlob = sqlite3_column_blob(stmt, 2);
			const auto embeddingSize = sqlite3_column_bytes(stmt, 2);
			const int numFloats = embeddingSize / sizeof(float);
			const auto *embeddingData = static_cast<const float *>(embeddingBlob);
			record.embedding = std::vector<float>(embeddingData, embeddingData + numFloats);

			record.source = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
			record.created = sqlite3_column_int(stmt, 4);
		} else {
			sqlite3_finalize(stmt);
			return std::nullopt; // No matching record found
		}

		sqlite3_finalize(stmt);

		return record;
	}

	EmbeddingAI::EmbeddingAI(int controlPort, int embeddingPort): controlPort(controlPort), embeddingPort(embeddingPort) {
		curl_global_init(CURL_GLOBAL_ALL);
	}

	EmbeddingAI::~EmbeddingAI() {
		curl_global_cleanup();
	}

	std::vector<float> EmbeddingAI::ExtractEmbeddingFromJson(const nlohmann::json &response)
	{
		std::vector<float> storageEmbedding;
		nlohmann::json jr = response["data"][0];
		const auto embedding = jr["embedding"];
		for (const auto &element : embedding) {
			const auto value = element.get<float>();
			storageEmbedding.push_back(value);
		}
		return storageEmbedding;
	}

	std::optional<nlohmann::json> EmbeddingAI::SendRetrieverRequest(const std::string& query)
	{
		nlohmann::json response;
		std::string response_body;
		bool success = false;

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Set the URL for the POST request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(embeddingPort) + "/embedding").c_str());

			// Specify the POST data
			// first wrap the query in a json object
			const nlohmann::json j = {
				{ "input", query }
			};
			const std::string json = j.dump();
			const size_t content_length = json.size();
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(content_length)).c_str());
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
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << std::endl << "cURL failure: " << curl_easy_strerror(res) << std::endl;
			} else {
				// Parse the response body as JSON
				response = nlohmann::json::parse(response_body);
				success = true;
			}
		}

		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	bool EmbeddingAI::SendHealthRequest()
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

	bool EmbeddingAI::SendInferenceRestartRequest()
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

	std::optional<nlohmann::json> EmbeddingAI::SendRetrieveModelMetadataRequest()
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

	bool OnInferenceProgressDefault(const nlohmann::json &metrics)
	{
		return true;
	}

	void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
	{
		inference_status = status;
	}

	void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{}

	bool EmbeddingAI::StartAI(const std::string &model)
	{
		try {
			auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);

			spdlog::info("(StartAI) Generating with model: {}", loader->modelName());

			const auto filename = std::filesystem::path(loader->getModelPath()).filename().string();
			const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
			if (!dli) {
				spdlog::error("(StartAI) Failed to parse download item name from safe file path");
				return false;
			}
			std::map<std::string, std::string> options;
			options["--port"] = std::to_string(embeddingPort);
			options["--model"] = loader->getModelPath();
			options["--alias"] = dli.value().filePath;
			options["--gpu-layers"] = "99";
			options["--embedding"] = "";

			// join pairs into a char** argv compatible array
			std::vector<std::string> args;
			args.emplace_back("generate");
			for (const auto &[option, value] : options) {
				args.push_back(option);
				if (!value.empty()) {
					args.push_back(value);
				}
			}
			owned_cstrings cargs(args);
			std::function<void()> requestShutdownInference;
			inference_status = WingmanItemStatus::unknown;
			std::thread inferenceThread([&]() {
				loader->run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
			});

			while (inference_status != WingmanItemStatus::inferring) {
				fmt::print("{}: {}\t\t\t\r", loader->modelName(), WingmanItem::toString(inference_status));
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			std::cout << std::endl;
			thread = std::move(inferenceThread);
			ai = std::move(loader);
			shutdown = std::move(requestShutdownInference);
			return true;
		} catch (const std::exception &e) {
			spdlog::error("(StartAI) Exception: {}", e.what());
			return false;
		}
	}

	void EmbeddingAI::StopAI()
	{
		shutdown();
		thread.join();
	}
} // namespace wingman::embedding
