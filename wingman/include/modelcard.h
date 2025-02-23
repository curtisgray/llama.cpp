#pragma once
namespace wingman {
	struct ModelCardFileInfo {
		std::string file_name;
		std::string file_url;
		std::string file_quant_method;
		int file_bits;
		double file_size; // GB
		double file_max_ram_required; // GB
		std::string file_use_case;
	};

	struct ModelInfo {
		std::string model_name;
		std::string model_creator;
		std::string model_type;
		std::vector<ModelCardFileInfo> provided_files;
	};

	ModelInfo readModelInfo(const std::string &modelCardPath);
	ModelInfo downloadModelInfo(const std::string &modelRepo);
} // namespace wingman
