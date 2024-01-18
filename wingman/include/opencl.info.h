#pragma once
#include <map>

namespace wingman::opencl {
	std::map< std::string, std::map<std::string, std::string> > GetClPlatformDevices();
	std::string GetGpuName();
}
