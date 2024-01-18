#pragma once
namespace wingman::opengl {
	bool GetGpuMemory(long &total, long &free);
	bool NVidiaMemoryInKb(long &total, long &free);
	bool AmdMemoryInKb(long &total, long &free);
}
