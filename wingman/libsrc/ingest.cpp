#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

#include "json.hpp"
#include "ingest.h"

namespace wingman::silk::ingestion {
	const auto PAGE_BREAK = "\f\n\f";

	// Function to split text into sentences
	std::vector<std::string> SplitIntoSentences(const std::string &text)
	{
		std::vector<std::string> sentences;
		std::string currentSentence;
		for (char c : text) {
			currentSentence += c;
			if (c == '.' || c == '!' || c == '?') {
				sentences.push_back(currentSentence);
				currentSentence = "";
			}
		}
		if (!currentSentence.empty()) {
			sentences.push_back(currentSentence);
		}
		return sentences;
	}

	// Function to chunk text into fixed-size chunks
	std::vector<std::string> ChunkText(const std::string &text, const int chunkSize)
	{
		std::vector<std::string> chunks;
		for (size_t i = 0; i < text.size(); i += chunkSize) {
			chunks.push_back(text.substr(i, chunkSize));
		}
		return chunks;
	}

	std::string FixUtf8String(const std::string &str)
	{
		const icu::UnicodeString ustr(str.c_str(), "UTF-8");

		std::string result;
		for (int i = 0; i < ustr.length(); ++i) {
			const UChar32 c = ustr.char32At(i);
			if (c >= 32 && c <= 126) { // ASCII printable characters
				result += static_cast<char>(c);
			} else {
				// Remove the non-printable characters completely
				result += "";
			}
		}
		return result;
	}

	std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
		ChunkPdfText(const std::string &pdfFilename, const int chunkSize, const int contextSize)
	{
		std::map<std::string, std::map<std::string, std::vector<std::string>>> result;
		std::map<std::string, std::vector<std::string>> chunks;

		const auto document = poppler::document::load_from_file(pdfFilename);
		if (!document) {
			std::cerr << "Failed to open PDF: " << pdfFilename << std::endl;
			return std::nullopt;
		}
		std::string pdfContent;
		for (int pageNum = 0; pageNum < document->pages(); ++pageNum) {
			const auto page = std::unique_ptr<poppler::page>(document->create_page(pageNum));
			if (!page) continue;

			std::string pageContent;

			for (const auto &textbox : page->text_list()) {
				pageContent += textbox.text().to_latin1();
				pageContent += " ";
			}

			// pageContent += "\n"; // Add a newline after each page
			// Add a special character to mark the end of each page
			// pageContent += PAGE_BREAK;

			pageContent = FixUtf8String(pageContent);

			// // Split page content into smaller chunks if it exceeds maxEmbeddingSize
			// std::vector<std::string> pageChunks = ChunkText(pageContent, contextSize * 3);
			// for (const auto &chunk : pageChunks) {
			// 	chunks["page"].push_back(chunk);
			// }
			chunks["page"].push_back(pageContent);

			pdfContent += pageContent;
		}

		for (const auto &page : chunks["page"]) {
			// Sentence chunks
			std::vector<std::string> sentences = SplitIntoSentences(page);
			for (const std::string &sentence : sentences) {
				chunks["sentence"].push_back(sentence);
			}
		}

		// Fixed-size chunks
		const std::vector<std::string> fixedSizeChunks = ChunkText(pdfContent, chunkSize);
		for (const std::string &chunk : fixedSizeChunks) {
			chunks["fixed_size"].push_back(chunk);
		}

		result[pdfFilename] = chunks;
		return result;
	}

	std::optional<std::map<std::string, std::map<std::string, std::vector<std::string>>>>
		ChunkPdfTextEx(const std::string &pdfFilename, const int chunkSize)
	{
		std::map<std::string, std::map<std::string, std::vector<std::string>>> result;
		std::map<std::string, std::vector<std::string>> chunks;

		const auto document = poppler::document::load_from_file(pdfFilename);
		if (!document) {
			std::cerr << "Failed to open PDF: " << pdfFilename << std::endl;
			return std::nullopt;
		}

		for (int pageNum = 0; pageNum < document->pages(); ++pageNum) {
			const auto page = std::unique_ptr<poppler::page>(document->create_page(pageNum));
			if (!page) continue;

			std::string pageContent;
			std::string paragraph;
			double previousBottom = 0.0;
			double previousRight = 0.0;

			for (const auto &textbox : page->text_list()) {
				const double top = textbox.bbox().top();
				const double bottom = textbox.bbox().bottom();
				const double left = textbox.bbox().left();
				const double right = textbox.bbox().right();

				// 1. Check for Vertical Spacing 
				if (previousBottom != 0.0 && (top - previousBottom) > (textbox.get_font_size() * 1.2)) {
					chunks["paragraph"].push_back(paragraph);
					paragraph.clear();
				}

				// 2. Check for a Significant Horizontal Jump (new column)
				if (previousRight != 0.0 && (left - previousRight) > (textbox.get_font_size() * 2.0)) {
					chunks["paragraph"].push_back(paragraph);
					paragraph.clear();
				}

				paragraph += textbox.text().to_latin1() + " ";
				previousBottom = bottom;
				previousRight = right;
			}

			if (!paragraph.empty()) {
				chunks["paragraph"].push_back(paragraph); // Add the last paragraph
			}

			pageContent = "";
			for (const auto &p : chunks["paragraph"])
				pageContent += p + "\n";

			// Page chunks
			chunks["page"].push_back(pageContent);

			// Sentence chunks
			std::vector<std::string> sentences = SplitIntoSentences(pageContent);
			for (const std::string &sentence : sentences) {
				chunks["sentence"].push_back(sentence);
			}

			// Fixed-size chunks 
			std::vector<std::string> fixedSizeChunks = ChunkText(pageContent, chunkSize);
			for (const std::string &chunk : fixedSizeChunks) {
				chunks["fixed_size"].push_back(chunk);
			}
		}
		result[pdfFilename] = chunks;
		return result;
	}
} // namespace wingman::silk::ingestion
