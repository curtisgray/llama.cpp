#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>

#include <sqlite3.h>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

namespace wingman::silk::ingestion {
	std::vector<std::string> SplitIntoSentences(const std::string &text);
	std::vector<std::string> ChunkText(const std::string &text, int chunkSize);
	std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
		ChunkPdfText(const std::string &pdfFilename, const int chunkSize, const int maxEmbeddingSize);
	std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
		ChunkPdfTextEx(const std::string &pdfFilename, const int chunkSize);

	using AnnoyIndex = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	struct EmbeddingRecord {
		int id;
		std::string chunk;
		std::vector<float> embedding;
		std::string source;
		int created;

	};
	class EmbeddingDb {

		const char *getCreateEmbeddingTableSql()
		{
			return "CREATE TABLE IF NOT EXISTS embeddings ("
				"id INTEGER PRIMARY KEY, "
				"chunk TEXT, "
				"embedding BLOB, "
				"source TEXT, "
				"created INTEGER DEFAULT (unixepoch('now')) NOT NULL)";
		}

		sqlite3 *openEmbeddingDatabase(const std::string &dbPath)
		{
			sqlite3 *ret = nullptr;
			char *errMsg = nullptr;

			int rc = sqlite3_open(dbPath.c_str(), &ret);
			if (rc) {
				std::cerr << "Can't open database: " << sqlite3_errmsg(ret) << std::endl;
				return nullptr;
			}

			const auto createTableSql = getCreateEmbeddingTableSql();
			rc = sqlite3_exec(ret, createTableSql, nullptr, nullptr, &errMsg);
			if (rc != SQLITE_OK) {
				std::cerr << "SQL error: " << errMsg << std::endl;
				sqlite3_free(errMsg);
				sqlite3_close(ret);
				return nullptr;
			}
			return ret;
		}

		void closeEmbeddingDatabase(sqlite3 *db)
		{
			sqlite3_close(db);
		}

		sqlite3 *db_;
		const std::string dbPath_;

	public:
		EmbeddingDb(std::string dbPath)
			: dbPath_(std::move(dbPath))
		{
			db_ = openEmbeddingDatabase(dbPath_);
			if (!db_) {
				throw std::runtime_error("Failed to open embedding database: " + dbPath_);
			}
		}

		~EmbeddingDb()
		{
			closeEmbeddingDatabase(db_);
		}

		size_t insertEmbeddingToDb(const std::string &chunk, const std::string &source, const std::vector<float> &embedding) const
		{
			sqlite3_stmt *stmt = nullptr;
			constexpr size_t id = -1;

			const std::string insertSql = "INSERT INTO embeddings (chunk, source, embedding) VALUES (?, ?, ?)";

			auto rc = sqlite3_prepare_v2(db_, insertSql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
				return id;
			}

			// Bind the text chunk
			rc = sqlite3_bind_text(stmt, 1, chunk.c_str(), -1, SQLITE_STATIC);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to bind text chunk: " << sqlite3_errmsg(db_) << std::endl;
				return id;
			}

			// Bind the source
			rc = sqlite3_bind_text(stmt, 2, source.c_str(), -1, SQLITE_STATIC);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to bind source: " << sqlite3_errmsg(db_) << std::endl;
				return id;
			}

			// Bind the embedding
			rc = sqlite3_bind_blob(stmt, 3, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to bind embedding: " << sqlite3_errmsg(db_) << std::endl;
				return id;
			}

			// Execute the statement
			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE) {
				sqlite3_reset(stmt);
				std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db_) << std::endl;
				return id;
			}

			sqlite3_finalize(stmt);

			return static_cast<size_t>(sqlite3_last_insert_rowid(db_));
		}

		std::optional<EmbeddingRecord> getEmbeddingById(const int id) const
		{
			sqlite3_stmt *stmt = nullptr;
			EmbeddingRecord record;

			const std::string selectSql = "SELECT id, chunk, embedding, source, created FROM embeddings WHERE id = ?";

			auto rc = sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
				return std::nullopt;
			}

			// Bind the ID
			rc = sqlite3_bind_int(stmt, 1, id);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to bind ID: " << sqlite3_errmsg(db_) << std::endl;
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
	};

	std::vector<size_t> QueryForNearestVectors(const std::string &annoyFilePath, const std::vector<float> &queryEmbedding, const int numNeighbors = 10);
	std::vector<float> ExtractEmbeddingFromJson(const nlohmann::json &response);
} // namespace wingman
