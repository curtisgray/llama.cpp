#include <iostream>

#include "opencl.info.h"

int main(int argc, char **argv)
{
	const auto gpuName = getGPUName();
	std::cout << "GPU Name: " << gpuName << std::endl;
	return 0;
}
