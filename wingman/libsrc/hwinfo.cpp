#include <memory>
#include <array>
#include <sstream>

#include "hwinfo.h"

std::string exec(const char *cmd)
{
	std::string result;
#if defined(LINUX) || defined(__linux__) || defined(__linux)
	std::array<char, 128> buffer;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
#endif
	return result;
}

GPUInfo getGPUInfo()
{
	GPUInfo info = {0, 0};
#if defined(NVIDIA_GPU)
	std::string output = exec("nvidia-smi --query-gpu=memory.total,memory.free --format=csv,noheader");
	std::istringstream ss(output);
	std::string token;

	std::getline(ss, token, ','); // Total memory
	info.totalMemoryMB = std::stoi(token);
	std::getline(ss, token, ','); // Free memory
	info.freeMemoryMB = std::stoi(token);
#endif
	return info;
}

RAMInfo getRAMInfo()
{
	std::string output = exec("free -m | grep Mem:");
	RAMInfo info;
	std::istringstream ss(output);
	std::string token;

	ss >> token; // Skip "Mem:"
	ss >> token; // Total memory
	info.totalMemoryMB = std::stoi(token);
	ss >> token; // Used memory (skip)
	ss >> token; // Free memory
	info.freeMemoryMB = std::stoi(token);

	return info;
}

HardwareInfo getHardwareInfo()
{
	HardwareInfo hwInfo;
	hwInfo.gpu = getGPUInfo();
	hwInfo.ram = getRAMInfo();
	return hwInfo;
}
