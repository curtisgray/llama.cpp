#pragma once
#include "hwinfo.h"

namespace wingman {
	struct Inferability {
		bool isInferable;
		int totalMemory;
		int availableMemory;
		int normalizedQuantizedMemRequired;
	};
	// Inferability CheckInferability(const AIModel &model, HardwareInfo hardwareInfo);
	Inferability CheckInferability(const AIModel &model, HardwareInfo hardwareInfo, int quantizationBits);
} // namespace wingman
