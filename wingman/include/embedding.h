#pragma once
#include <optional>
#include <sqlite3.h>

#include "llama.hpp"

namespace wingman::silk::embedding {

	struct EmbeddingRecord {
		int id;
		std::string chunk;
		std::vector<float> embedding;
		std::string source;
		int created;

	};
	class EmbeddingDb {
		static const char *getCreateEmbeddingTableSql();

		sqlite3 *openEmbeddingDatabase(const std::string &dbPath);

		void closeEmbeddingDatabase(sqlite3 *db);

		sqlite3 *db_;
		const std::string dbPath_;

	public:
		EmbeddingDb(std::string dbPath);

		~EmbeddingDb();

		size_t insertEmbeddingToDb(const std::string &chunk, const std::string &source, const std::vector<float> &embedding) const;

		std::optional<EmbeddingRecord> getEmbeddingById(const int id) const;
	};

	class EmbeddingAI {
		int controlPort = 6568;	// TODO: give ingest its own control server
		int embeddingPort = 45678;
	public:

		std::shared_ptr<ModelLoader> ai;
		std::function<void()> shutdown;
		std::thread thread;

		EmbeddingAI(int controlPort, int embeddingPort);

		~EmbeddingAI();

		std::vector<float> ExtractEmbeddingFromJson(const nlohmann::json &response);
		std::optional<nlohmann::json> SendRetrieverRequest(const std::string& query);
		bool SendHealthRequest();
		bool SendInferenceRestartRequest();
		std::optional<nlohmann::json> SendRetrieveModelMetadataRequest();
		bool StartAI(const std::string &model = "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf");
		void StopAI();
	};
} // namespace wingman::embedding
