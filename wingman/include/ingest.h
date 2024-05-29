#pragma once
#include <string>
#include <vector>

namespace wingman::silk::ingestion {
	std::vector<std::string> SplitIntoSentences(const std::string &text);
	std::vector<std::string> ChunkText(const std::string &text, int chunkSize);
	std::vector<std::string> ChunkPdfText(const std::string &pdfFilename, const int chunkSize, const int overlap);
	// std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
	// 	ChunkPdfText(const std::string &pdfFilename, const int chunkSize, const int contextSize);
	// std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
	// 	ChunkPdfTextEx(const std::string &pdfFilename, const int chunkSize);
} // namespace wingman
