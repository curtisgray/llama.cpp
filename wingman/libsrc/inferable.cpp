#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <sstream>

#include "types.h"
#include "inferable.h"
#include "hwinfo.h"

namespace wingman {
	// Helper functions
	bool IsNumber(const std::string &value)
	{
		std::istringstream iss(value);
		double number;
		// Attempt to convert to a double
		iss >> number;

		// Check if the entire string was successfully parsed and no invalid characters were found
		return iss.eof() && !iss.fail();
	}

	double ToNumber(const std::string &value)
	{
		std::istringstream iss(value);
		double number = 0;
		// Attempt to convert to a double
		iss >> number;

		if (!iss.fail() && iss.eof()) {
			return number; // Successfully parsed
		}
		return std::numeric_limits<double>::quiet_NaN(); // Return NaN to indicate parse failure
	}

	Inferability CheckInferability(const AIModel &model, HardwareInfo hardwareInfo, int quantizationBits)
	{
		if (model.size.empty()) {
			spdlog::debug("Model '{}' has EMPTY size. Will not be analyzed to see if it is inferable.", model.name);
			return { false, -1, -1, -1 };
		}

		if (quantizationBits < 1 || quantizationBits > 32) {
			spdlog::error("Invalid quantization bits: {}. Must be between 1 and 32.", quantizationBits);
			return { false, -1, -1, -1 };
		}

		int totalMemory;
		int availableMemory;

		// Determine memory source (GPU or RAM)
		if (hardwareInfo.gpu.totalMemoryMB > 0) {
			totalMemory = hardwareInfo.gpu.totalMemoryMB;
			availableMemory = hardwareInfo.gpu.freeMemoryMB > 0 ? hardwareInfo.gpu.freeMemoryMB : totalMemory;
		} else {
			totalMemory = hardwareInfo.cpu.totalMemoryMB;
			availableMemory = hardwareInfo.cpu.freeMemoryMB;
		}

		// Extract parameter size and units
		const char parameterSizeIndicator = model.size.back();
		const bool isMoe = model.size.find('x') != std::string::npos;
		int64_t sizeMultiplier = -1;
		switch (parameterSizeIndicator) {
			case 'K': sizeMultiplier = 1000; break;
			case 'M': sizeMultiplier = 1000000; break;
			case 'B': sizeMultiplier = 1000000000; break;
			case 'T': sizeMultiplier = 1000000000000; break;
			case 'Q': sizeMultiplier = 1000000000000000; break;
		}
		if (sizeMultiplier == -1) {
			return { false, totalMemory, availableMemory, -1 };
		}

		int moeMultiplier = 1;
		double parameterValue = 0;
		if (isMoe) {
			const auto parts = util::splitString(model.size, 'x');
			moeMultiplier = ToNumber(parts[0]);
			parameterValue = ToNumber(parts[1].substr(0, parts[1].length() - 1)) * moeMultiplier;
		} else {
			parameterValue = ToNumber(model.size.substr(0, model.size.length() - 1));
		}

		double effectiveQuantizationBits = quantizationBits;
		if (quantizationBits == 1) {
			effectiveQuantizationBits = 1.58; // Special case for 1-bit quantization
		}

		spdlog::trace("Parameter Value: {}", parameterValue);
		spdlog::trace("Quantized Bits: {}", effectiveQuantizationBits);

		const double quantizedSize = parameterValue * effectiveQuantizationBits * sizeMultiplier / 8;
		spdlog::trace("Quantized Size: {}", quantizedSize);
		const double quantizedMemRequired = quantizedSize / sizeMultiplier;
		spdlog::trace("Quantized Memory Required: {}", quantizedMemRequired);
		// const double normalizedQuantizedMemRequired = quantizedMemRequired * 1024;
		const int normalizedQuantizedMemRequired = static_cast<int>(std::ceil(quantizedMemRequired * 1024));
		spdlog::trace("Normalized Quantized Memory Required to Run '{}': {}", model.name, normalizedQuantizedMemRequired);
		const double memoryDelta = availableMemory - normalizedQuantizedMemRequired;

		// Log and return inferability information
		spdlog::trace("Model '{}' ({}) {} inferable. Available Memory: {} Quantized Need: {} Delta: {}",
					  model.name, model.size,
					  normalizedQuantizedMemRequired <= availableMemory ? "is" : "is not",
					  availableMemory, normalizedQuantizedMemRequired, memoryDelta);

		return { normalizedQuantizedMemRequired <= availableMemory,
				totalMemory,
				availableMemory,
				normalizedQuantizedMemRequired };
	}

	Inferability CheckInferability(const AIModel &model, HardwareInfo hardwareInfo)
	{
		if (model.size.empty()) {
			spdlog::debug("Model '{}' has EMPTY size. Will not be analyzed to see if it is inferable.", model.name);
			return { false, -1, -1, -1 };
		}

		int totalMemory;
		int availableMemory;

		// Determine memory source (GPU or RAM)
		if (hardwareInfo.gpu.totalMemoryMB > 0) {
			totalMemory = hardwareInfo.gpu.totalMemoryMB;
			if (hardwareInfo.gpu.freeMemoryMB > 0) {
				availableMemory = hardwareInfo.gpu.freeMemoryMB;
			} else {
				availableMemory = hardwareInfo.gpu.totalMemoryMB;
			}
		} else {
			totalMemory = hardwareInfo.cpu.totalMemoryMB;
			availableMemory = hardwareInfo.cpu.freeMemoryMB;
		}

		spdlog::trace("Available Memory to run {} ({}): {}", model.name, model.size, availableMemory);
		if (availableMemory == -1) {
			return { false, totalMemory, -1, -1 };
		}

		// Extract parameter size and units
		const char parameterSizeIndicator = model.size.back();
		const bool isMoe = model.size.find('x') != std::string::npos;
		int64_t sizeMultiplier = -1;
		switch (parameterSizeIndicator) {
			case 'K': sizeMultiplier = 1000; break;
			case 'M': sizeMultiplier = 1000000; break;
			case 'B': sizeMultiplier = 1000000000; break;
			case 'T': sizeMultiplier = 1000000000000; break;
			case 'Q': sizeMultiplier = 1000000000000000; break;
		}
		if (sizeMultiplier == -1) {
			return { false, totalMemory, availableMemory, -1 };
		}

		int moeMultiplier = 1;
		double parameterValue = 0;
		if (isMoe) {
			// Extract MoE multiplier and parameter size
			const auto parts = util::splitString(model.size, 'x');
			moeMultiplier = ToNumber(parts[0]);
			parameterValue = ToNumber(parts[1].substr(0, parts[1].length() - 1)) * moeMultiplier;
		} else {
			parameterValue = ToNumber(model.size.substr(0, model.size.length() - 1));
		}

		constexpr int quantizedBits = 4;
		spdlog::trace("Parameter Value: {}", parameterValue);
		spdlog::trace("Quantized Bits: {}", quantizedBits);

		const double quantizedSize = parameterValue * quantizedBits * sizeMultiplier / 8;
		spdlog::trace("Quantized Size: {}", quantizedSize);
		const double quantizedMemRequired = quantizedSize / sizeMultiplier;
		spdlog::trace("Quantized Memory Required: {}", quantizedMemRequired);
		// const double normalizedQuantizedMemRequired = quantizedMemRequired * 1024;
		const int normalizedQuantizedMemRequired = static_cast<int>(std::ceil(quantizedMemRequired * 1024));
		spdlog::trace("Normalized Quantized Memory Required to Run '{}': {}", model.name, normalizedQuantizedMemRequired);
		const double memoryDelta = availableMemory - normalizedQuantizedMemRequired;

		// Log and return inferability information
		spdlog::trace("Model '{}' ({}) {} inferable. Available Memory: {} Quantized Need: {} Delta: {}",
					  model.name, model.size,
					  normalizedQuantizedMemRequired <= availableMemory ? "is" : "is not",
					  availableMemory, normalizedQuantizedMemRequired, memoryDelta);

		return { normalizedQuantizedMemRequired <= availableMemory,
				totalMemory,
				availableMemory,
				normalizedQuantizedMemRequired };
	}

} // namespace wingman 
