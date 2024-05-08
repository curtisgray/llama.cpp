#ifdef GGML_USE_CUDA
#  include "ggml-cuda.h"
#elif defined(GGML_USE_CLBLAST)
#  include "ggml-opencl.h"
#elif defined(GGML_USE_VULKAN)
#  include "ggml-vulkan.h"
#elif defined(GGML_USE_SYCL)
#  include "ggml-sycl.h"
#elif defined(GGML_USE_KOMPUTE)
#   include "ggml-kompute.h"
#endif

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <fstream>
#endif

#include "hwinfo.h"

namespace wingman {
	HardwareInfo GetHardwareInfo()
	{
		HardwareInfo info = { { -1,-1}, {-1,-1} };

		// Get RAM information
#if defined(_WIN32)
		MEMORYSTATUSEX memInfo;
		memInfo.dwLength = sizeof(memInfo);
		GlobalMemoryStatusEx(&memInfo);
		info.cpu.totalMemoryMB = static_cast<int>(memInfo.ullTotalPhys / 1048576);
		info.cpu.freeMemoryMB = static_cast<int>(memInfo.ullAvailPhys / 1048576);
#elif defined(__APPLE__)
		int64_t totalMemory;
		size_t length = sizeof(totalMemory);
		sysctlbyname("hw.memsize", &totalMemory, &length, NULL, 0);
		info.cpu.totalMemoryMB = static_cast<int>(totalMemory / 1048576);

		// Getting free memory on macOS is more involved and requires parsing vm_stat output
		// This is a simplified approach that may not be completely accurate
		vm_statistics_data_t vmStats;
		mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
		host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vmStats, &count);
		info.cpu.freeMemoryMB = static_cast<int>((vmStats.free_count + vmStats.inactive_count) * vm_page_size / 1048576);
#elif defined(__linux__)
		struct sysinfo memInfo;
		sysinfo(&memInfo);
		info.cpu.totalMemoryMB = static_cast<int>(memInfo.totalram / 1048576);
		info.cpu.freeMemoryMB = static_cast<int>(memInfo.freeram / 1048576);
#else
#error "Unsupported platform for RAM information"
#endif

	// Get GPU information using llama.cpp library
#ifdef GGML_USE_CUDA
		int deviceCount = ggml_backend_cuda_get_device_count();
		if (deviceCount > 0) {
			size_t free, total;
			ggml_backend_cuda_get_device_memory(0, &free, &total); // Assuming we want info for the first GPU
			info.gpu.totalMemoryMB = static_cast<int>(total / 1048576);
			info.gpu.freeMemoryMB = static_cast<int>(free / 1048576);
		}
#elif defined(GGML_USE_SYCL)
		int deviceCount = ggml_backend_sycl_get_device_count();
		if (deviceCount > 0) {
			size_t free, total;
			ggml_backend_sycl_get_device_memory(0, &free, &total); // Assuming we want info for the first GPU
			info.gpu.totalMemoryMB = static_cast<int>(total / 1048576);
			info.gpu.freeMemoryMB = static_cast<int>(free / 1048576);
		}
#elif defined(GGML_USE_VULKAN)
		int deviceCount = ggml_backend_vk_get_device_count();
		if (deviceCount > 0) {
			size_t free, total;
			ggml_backend_vk_get_device_memory(0, &free, &total); // Assuming we want info for the first GPU
			info.gpu.totalMemoryMB = static_cast<int>(total / 1048576);
			info.gpu.freeMemoryMB = static_cast<int>(free / 1048576);
		}
#endif

		return info;
	}
} // namespace wingman
