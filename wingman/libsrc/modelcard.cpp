#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "modelcard.h"
#include "curl.h"

ModelInfo extractModelCardData(const std::string &modelCard)
{
	ModelInfo modelInfo;
	std::smatch matches;

	// Regex patterns for model details
	std::regex model_name_regex("model_name: ([^\\n]+)");
	std::regex model_creator_regex("model_creator: ([^\\n]+)");
	std::regex model_type_regex("model_type: ([^\\n]+)");

	// Extract model details
	if (std::regex_search(modelCard, matches, model_name_regex) && matches.size() > 1) {
		modelInfo.model_name = matches[1].str();
	}
	if (std::regex_search(modelCard, matches, model_creator_regex) && matches.size() > 1) {
		modelInfo.model_creator = matches[1].str();
	}
	if (std::regex_search(modelCard, matches, model_type_regex) && matches.size() > 1) {
		modelInfo.model_type = matches[1].str();
	}

	// Adjusted regex pattern to extract file name and URL
	std::regex files_regex("\\| \\[([^\\]]+)\\]\\(([^\\)]+)\\) \\| ([^|]+) \\| (\\d+) \\| ([\\d.]+) GB\\| ([\\d.]+) GB \\| ([^|]+) \\|");
	auto files_begin = std::sregex_iterator(modelCard.begin(), modelCard.end(), files_regex);
	auto files_end = std::sregex_iterator();

	for (std::sregex_iterator i = files_begin; i != files_end; ++i) {
		ModelCardFileInfo file;
		file.file_name = (*i)[1].str();
		file.file_url = (*i)[2].str(); // Capture URL
		file.file_quant_method = (*i)[3].str();
		file.file_bits = std::stoi((*i)[4].str());
		file.file_size = std::stod((*i)[5].str());
		file.file_max_ram_required = std::stod((*i)[6].str());
		file.file_use_case = (*i)[7].str();
		modelInfo.provided_files.push_back(file);
	}

	return modelInfo;
}

std::string readFileContent(const std::string &filePath)
{
	std::ifstream file(filePath);
	std::string content;

	if (file) {
		std::stringstream buffer;
		buffer << file.rdbuf(); // Read the file's buffer into a stringstream
		content = buffer.str(); // Convert the stringstream into a string
	} else {
		std::cerr << "Could not open file: " << filePath << std::endl;
	}

	return content;
}

ModelInfo readModelInfo(const std::string &modelCardPath)
{
	std::string modelCard = readFileContent(modelCardPath);
	return extractModelCardData(modelCard);
}

ModelInfo downloadModelInfo(const std::string &modelRepo)
{
	try {
		wingman::curl::Request request;

		request.url = fmt::format("https://huggingface.co/{}/resolve/main/{}", modelRepo, "README.md");
		request.method = "GET";
		const auto response = Fetch(request);
		return extractModelCardData(response.text());
	} catch (std::exception &e) {
		spdlog::error("Failed to get models: {}", e.what());
		return {};
	}
}
