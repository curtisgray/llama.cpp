#include <optional>
#include <string>
#include <vector>

#include "embedding.h"
#include "embedding.index.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

namespace wingman::silk::embedding {
	using annoy_index = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;
	std::unique_ptr<annoy_index> index;
	std::unique_ptr<EmbeddingDb> db;

	EmbeddingIndex::EmbeddingIndex(const std::string &memoryBankName, const int dimensions)
		: memoryBankName(memoryBankName), dimensions(dimensions), treeSize(dimensions * 2)
	{
		const auto wingmanHome = GetWingmanHome();

		annoyFilePath = (wingmanHome / "data" / std::filesystem::path(memoryBankName + ".ann").filename().string()).string();
		dbPath = (wingmanHome / "data" / std::filesystem::path(memoryBankName + ".db").filename().string()).string();
		index = std::make_unique<annoy_index>(static_cast<int>(dimensions));
		db = std::make_unique<EmbeddingDb>(dbPath);
	}

	void EmbeddingIndex::load() const
	{
		index->load(annoyFilePath.c_str());
	}

	void EmbeddingIndex::init() const
	{
		index->on_disk_build(annoyFilePath.c_str());
	}

	size_t EmbeddingIndex::add(
		const std::string &chunk, const std::string &source, const std::vector<float> &embedding)
	{
		const auto id = db->insertEmbeddingToDb(chunk, source, embedding);
		index->add_item(static_cast<size_t>(id), embedding.data());
		return id;
	}

	void EmbeddingIndex::build() const
	{
		const auto treeSize = static_cast<int>(dimensions * 2);
		std::cout << "Building annoy index of " << treeSize << " trees..." << std::endl;
		index->build(treeSize);
	}

	int EmbeddingIndex::getDimensions() const
	{
		return dimensions;
	}

	int EmbeddingIndex::getTreeSize() const
	{
		return treeSize;
	}

	void EmbeddingIndex::remove() const
	{
		std::filesystem::remove(annoyFilePath);
		std::filesystem::remove(dbPath);
	}

	std::string EmbeddingIndex::getMemoryBankName() const { return memoryBankName; }

	std::optional<std::vector<silk::embedding::Embedding>> EmbeddingIndex::getEmbeddings(
		const nlohmann::json &embedding,
		const int max)
	{
		auto ret = std::vector<silk::embedding::Embedding>();
		size_t count = 0;
		auto queryEmbedding = EmbeddingAI::extractEmbeddingFromJson(embedding);

		// Retrieve nearest neighbors
		std::vector<size_t> neighborIndices;
		std::vector<float> distances;
		index->get_nns_by_vector(queryEmbedding.data(), 1000, -1, &neighborIndices, &distances);

		// Create a vector of pairs to store index and distance together
		std::vector<std::pair<size_t, float>> neighbors;
		for (size_t i = 0; i < neighborIndices.size(); ++i) {
			neighbors.emplace_back(neighborIndices[i], distances[i]);
		}

		// Sort the neighbors by distance (ascending order)
		std::sort(neighbors.begin(), neighbors.end(),
			[](const auto &a, const auto &b) { return a.second < b.second; });
		if (max == -1) {
			count = neighbors.size();
		} else {
			count = std::min<size_t>(max, neighbors.size());
		}
		for (size_t i = 0; i < count; ++i) {
			const auto &[id, distance] = neighbors[i];
			// Retrieve the data associated with the neighbor index from SQLite
			const auto row = db->getEmbeddingById(static_cast<sqlite3_int64>(id));
			if (row) {
				silk::embedding::Embedding e;
				e.record = row.value();
				e.distance = distance;
				ret.push_back(e);
			}
		}
		return ret;
	}

	nlohmann::json EmbeddingIndex::getSilkContext(const std::vector<silk::embedding::Embedding> &embeddings)
	{
		nlohmann::json silkContext;
		for (const auto &[record, distance] : embeddings) {
			silkContext.push_back({
				{ "id", record.id },
				{ "chunk", record.chunk },
				{ "source", record.source },
				{ "distance", distance }
			});
		}
		return silkContext;
	}

	nlohmann::json EmbeddingIndex::getSilkContext(
		const nlohmann::json &embedding,
		const int max)
	{
		const auto embeddings = getEmbeddings(embedding, max);
		if (!embeddings) {
			throw std::runtime_error("Failed to retrieve embeddings");
		}
		return getSilkContext(embeddings.value());
	}
} // namespace wingman::silk::embedding
