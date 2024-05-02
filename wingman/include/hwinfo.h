#pragma once
#include "json.hpp"

namespace wingman {
	struct HardwareInfo {
		struct Memory {
			int totalMemoryMB; // Total GPU memory in MB
			int freeMemoryMB;  // Free GPU memory in MB
		} gpu, cpu;
	};

	inline bool operator==(const HardwareInfo::Memory &lhs, const HardwareInfo::Memory &rhs)
	{
		return lhs.totalMemoryMB == rhs.totalMemoryMB && lhs.freeMemoryMB == rhs.freeMemoryMB;
	}

	inline bool operator==(const HardwareInfo &lhs, const HardwareInfo &rhs)
	{
		return lhs.gpu == rhs.gpu && lhs.cpu == rhs.cpu;
	}

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HardwareInfo::Memory, totalMemoryMB, freeMemoryMB)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HardwareInfo, gpu, cpu)

	HardwareInfo GetHardwareInfo();
} // namespace wingman
