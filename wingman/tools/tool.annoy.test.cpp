#include <random>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include "types.h"

using AnnoyIndex = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

std::vector<float> GenerateRandomEmbedding(size_t size)
{
	std::vector<float> embeddings;
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

	for (size_t i = 0; i < size; ++i)
		embeddings.push_back(dist(gen));
	return embeddings;
}

void main(void)
{
	const auto home = wingman::GetWingmanHome();
	const std::string annoyFilePath = (home / "data" / std::filesystem::path("embeddings.ann").filename().string()).string();
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

	AnnoyIndex index(0);

	index.on_disk_build("embedding.ann");
	auto embeddings = GenerateRandomEmbedding(384);
	index.add_item(0, embeddings.data());
	embeddings = GenerateRandomEmbedding(384);
	index.add_item(1, embeddings.data());
	embeddings = GenerateRandomEmbedding(384);
	index.add_item(2, embeddings.data());
	index.build(10);
}
