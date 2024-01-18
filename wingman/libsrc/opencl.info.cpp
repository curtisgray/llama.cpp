// copyright 2013 Jens Schwarzer (schwarzer@schwarzer.dk), heavily modified by Curtis Gray 2023
// OpenCL info dump
#define CL_HPP_ENABLE_EXCEPTIONS            // enable exceptions

#if defined(__APPLE__) || defined(__MACOSX)
// #  include <OpenCL/opencl.hpp>
// #  include <CL/opencl.h>
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#include <CL/cl2.hpp>
#else
#define CL_HPP_TARGET_OPENCL_VERSION 300
#  include <CL/opencl.hpp>
#endif
#include <map>
#include <string>
#include <vector>

#include <iostream>
#include <numeric>

using std::cout;

#define P(obj, w) ret[platformName][#w] = (obj).getInfo<w>()
#define Pbool(obj, w) ret[platformName][#w] = static_cast<bool>((obj).getInfo<w>()) ? "true" : "false"

#define PbitmapStart(obj, w) { unsigned bitmap = (obj).getInfo<w>(); std::string key = #w; std::vector<std::string> list;
#define PbitmapTest(w) if (bitmap & (w)) list.push_back(#w)
#define PbitmapEnd ret[platformName][key] = join(list);}

#define PconstStart(obj, w) { unsigned constant = (obj).getInfo<w>(); std::string key = #w; std::vector<std::string> list;
#define PconstTest(w) if (constant == (w)) list.push_back(#w);
#define PconstEnd ret[platformName][key] = join(list);}

std::string join(std::vector<std::string> list, const std::string &delimiter = ", ")
{
	if (list.empty())
		return "";
	return std::accumulate(std::next(list.begin()), list.end(), list[0],
		[&](const std::string &a, const std::string &b) {
		return a + delimiter + b;
	}
	);
}

namespace wingman::opencl {
	std::map< std::string, std::map<std::string, std::string> > GetClPlatformDevices()
	{
		std::map < std::string, std::map<std::string, std::string> > ret;
		try {
			std::vector<cl::Platform> platforms;
			(void)cl::Platform::get(&platforms);

			// dump platform information
			for (const auto &platform : platforms) {
				const auto platformName = platform.getInfo<CL_PLATFORM_NAME>();

				std::vector<cl::Device> devices;
				(void)platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);

				// dump device information
				for (const auto &device : devices) {
					PbitmapStart(device, CL_DEVICE_TYPE);
					PbitmapTest(CL_DEVICE_TYPE_CPU);
					PbitmapTest(CL_DEVICE_TYPE_GPU);
					PbitmapTest(CL_DEVICE_TYPE_ACCELERATOR);
					PbitmapTest(CL_DEVICE_TYPE_DEFAULT);
					PbitmapTest(CL_DEVICE_TYPE_CUSTOM);
					PbitmapEnd;

					P(device, CL_DEVICE_VENDOR_ID);
					P(device, CL_DEVICE_MAX_COMPUTE_UNITS);
					P(device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS);

					{
						std::vector<size_t> sizes = device.getInfo<CL_DEVICE_MAX_WORK_ITEM_SIZES>();
						const auto key = "CL_DEVICE_MAX_WORK_ITEM_SIZES";
						auto list = std::vector<std::string>();
						for (auto size : sizes) {
							list.push_back(std::to_string(size));
						}
						ret[platformName][key] = join(list);
					}

					P(device, CL_DEVICE_MAX_WORK_GROUP_SIZE);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE);
					P(device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_INT);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE);
					P(device, CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF);
					P(device, CL_DEVICE_MAX_CLOCK_FREQUENCY);
					P(device, CL_DEVICE_ADDRESS_BITS);
					P(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE);

					Pbool(device, CL_DEVICE_IMAGE_SUPPORT);

					P(device, CL_DEVICE_MAX_READ_IMAGE_ARGS);
					P(device, CL_DEVICE_MAX_WRITE_IMAGE_ARGS);
					P(device, CL_DEVICE_IMAGE2D_MAX_WIDTH);
					P(device, CL_DEVICE_IMAGE2D_MAX_HEIGHT);
					P(device, CL_DEVICE_IMAGE3D_MAX_WIDTH);
					P(device, CL_DEVICE_IMAGE3D_MAX_HEIGHT);
					P(device, CL_DEVICE_IMAGE3D_MAX_DEPTH);
					P(device, CL_DEVICE_MAX_SAMPLERS);
					P(device, CL_DEVICE_MAX_PARAMETER_SIZE);
					P(device, CL_DEVICE_MEM_BASE_ADDR_ALIGN);

					PbitmapStart(device, CL_DEVICE_SINGLE_FP_CONFIG);
					PbitmapTest(CL_FP_DENORM);
					PbitmapTest(CL_FP_INF_NAN);
					PbitmapTest(CL_FP_ROUND_TO_NEAREST);
					PbitmapTest(CL_FP_ROUND_TO_ZERO);
					PbitmapTest(CL_FP_ROUND_TO_INF);
					PbitmapTest(CL_FP_FMA);
					PbitmapTest(CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
					PbitmapTest(CL_FP_SOFT_FLOAT);
					PbitmapEnd;

					PbitmapStart(device, CL_DEVICE_DOUBLE_FP_CONFIG);
					PbitmapTest(CL_FP_DENORM);
					PbitmapTest(CL_FP_INF_NAN);
					PbitmapTest(CL_FP_ROUND_TO_NEAREST);
					PbitmapTest(CL_FP_ROUND_TO_ZERO);
					PbitmapTest(CL_FP_ROUND_TO_INF);
					PbitmapTest(CL_FP_FMA);
					PbitmapTest(CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
					PbitmapTest(CL_FP_SOFT_FLOAT);
					PbitmapEnd;

					PconstStart(device, CL_DEVICE_GLOBAL_MEM_CACHE_TYPE);
					PconstTest(CL_NONE);
					PconstTest(CL_READ_ONLY_CACHE);
					PconstTest(CL_READ_WRITE_CACHE);
					PconstEnd;

					P(device, CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE);
					P(device, CL_DEVICE_GLOBAL_MEM_CACHE_SIZE);
					P(device, CL_DEVICE_GLOBAL_MEM_SIZE);
					P(device, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE);
					P(device, CL_DEVICE_MAX_CONSTANT_ARGS);

					PconstStart(device, CL_DEVICE_LOCAL_MEM_TYPE);
					PconstTest(CL_NONE);
					PconstTest(CL_LOCAL);
					PconstTest(CL_GLOBAL);
					PconstEnd;

					P(device, CL_DEVICE_LOCAL_MEM_SIZE);

					Pbool(device, CL_DEVICE_ERROR_CORRECTION_SUPPORT);

					P(device, CL_DEVICE_PROFILING_TIMER_RESOLUTION);

					Pbool(device, CL_DEVICE_ENDIAN_LITTLE);
					Pbool(device, CL_DEVICE_AVAILABLE);
					Pbool(device, CL_DEVICE_COMPILER_AVAILABLE);

					PbitmapStart(device, CL_DEVICE_EXECUTION_CAPABILITIES);
					PbitmapTest(CL_EXEC_KERNEL);
					PbitmapTest(CL_EXEC_NATIVE_KERNEL);
					PbitmapEnd;

					PbitmapStart(device, CL_DEVICE_QUEUE_PROPERTIES);
					PbitmapTest(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
					PbitmapTest(CL_QUEUE_PROFILING_ENABLE);
					PbitmapEnd;

					P(device, CL_DEVICE_NAME);
					P(device, CL_DEVICE_VENDOR);
					P(device, CL_DRIVER_VERSION);
					P(device, CL_DEVICE_PROFILE);
					P(device, CL_DEVICE_VERSION);
					P(device, CL_DEVICE_OPENCL_C_VERSION);
					P(device, CL_DEVICE_EXTENSIONS);
				}
			}
		} catch (cl::Error err) {
		}
		return ret;
	}

	std::string GetGpuName()
	{
		const auto platforms = GetClPlatformDevices();
		std::string device_name = "unknown";
		if (!platforms.empty())
			for (const auto &platform : platforms)
				for (const auto &[key, value] : platform.second) {
					// check if this is a GPU device, if so, get the device name and break
					if (key == "CL_DEVICE_TYPE" && value == "CL_DEVICE_TYPE_GPU") {
						for (const auto &[k, v] : platform.second) {
							if (k == "CL_DEVICE_NAME") {
								device_name = v;
								break;
							}
						}
						break;
					}
				}
		return device_name;
	}
} // namespace wingman::opencl
