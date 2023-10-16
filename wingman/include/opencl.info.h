#pragma once
#include <map>
#include <CL/opencl.hpp>

//std::map< cl::Platform, std::map<std::string, std::string>> getCLPlatformDevices();
std::map< std::string, std::map<std::string, std::string> > getCLPlatformDevices();
std::string getGPUName();
