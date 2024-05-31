#pragma once
#include <optional>
#include <sqlite3.h>

#include "llama_integration.h"

namespace wingman::silk::embedding {

	struct EmbeddingRecord {
		int id;
		std::string chunk;
		std::vector<float> embedding;
		std::string source;
		int created;
		int chunkLength;
	};

	class EmbeddingDb {
		static const char *getCreateEmbeddingTableSql();

		static sqlite3 *openEmbeddingDatabase(const std::string &dbPath);

		static void closeEmbeddingDatabase(sqlite3 *db);

		sqlite3 *db_;
		const std::string dbPath_;

	public:
		EmbeddingDb(std::string dbPath);

		~EmbeddingDb();

		size_t insertEmbeddingToDb(const std::string &chunk, const std::string &source, const std::vector<float> &embedding) const;

		std::optional<EmbeddingRecord> getEmbeddingById(const sqlite3_int64 id) const;
	};

	class EmbeddingAI {
		int controlPort = -1;
		int embeddingPort = -1;
		orm::ItemActionsFactory& actionsFactory;
	public:

		std::shared_ptr<ModelLoader> ai;
		std::function<void()> shutdown;
		std::thread thread;

		EmbeddingAI(int controlPort, int embeddingPort, orm::ItemActionsFactory &actions);
		EmbeddingAI(int embeddingPort, orm::ItemActionsFactory &actions);

		~EmbeddingAI();

		static std::vector<float> extractEmbeddingFromJson(const nlohmann::json &response);
		std::optional<nlohmann::json> sendRetrieverRequest(const std::string& query) const;
		bool sendHealthRequest() const;
		bool sendInferenceRestartRequest() const;
		std::optional<nlohmann::json> sendRetrieveModelMetadataRequest() const;
		bool start(const std::string &model = "CompendiumLabs/bge-base-en-v1.5-gguf/bge-base-en-v1.5-q8_0.gguf");
		void stop();
	};

	class EmbeddingCalc {
	public:
		static float dotProduct(const float *x, const float *y, size_t length)
		{
			float result = 0.0f;
			for (size_t i = 0; i < length; ++i) {
				result += x[i] * y[i];
			}
			return result;
		}

		static float dotProduct(const std::vector<float> &x, const std::vector<float> &y)
		{
			return dotProduct(x.data(), y.data(), x.size());
		}

		static float dotProduct(const std::vector<float> &v)
		{
			return dotProduct(v.data(), v.data(), v.size());
		}

		static float cosineSimilarity(const float *x, const float *y, size_t length)
		{
			const float dot = dotProduct(x, y, length);
			const float normX = dotProduct(x, x, length);
			const float normY = dotProduct(y, y, length);
			return dot / (std::sqrt(normX) * std::sqrt(normY));
		}

		static float cosineSimilarity(const std::vector<float> &x, const std::vector<float> &y)
		{
			return cosineSimilarity(x.data(), y.data(), x.size());
		}

		static float cosineSimilarity(const std::vector<float> &v)
		{
			return cosineSimilarity(v.data(), v.data(), v.size());
		}
	};

} // namespace wingman::embedding
