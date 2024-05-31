#pragma once
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "embedding.h"

namespace wingman::silk::embedding {
	struct Embedding {
		EmbeddingRecord record;
		float distance;
	};

	class EmbeddingIndex {
		std::string memoryBankName;
		std::string annoyFilePath;
		std::string dbPath;
		int dimensions;
		int treeSize;
	public:
		std::optional<std::vector<Embedding>> getEmbeddings(
			const nlohmann::json &embedding,
			const int max = -1);

		nlohmann::json getSilkContext(const std::vector<Embedding> &embeddings);

		nlohmann::json getSilkContext(
			const nlohmann::json &embedding,
			const int max = -1);

		EmbeddingIndex(const std::string &memoryBankName, int dimensions);
		void load() const;
		void init() const;
		size_t add(const std::string &chunk, const std::string &source, const std::vector<float> &embedding);
		void build() const;
		int getDimensions() const;
		int getTreeSize() const;
		void remove() const;
		std::string getMemoryBankName() const;
	};
} // namespace wingman::silk::embedding
