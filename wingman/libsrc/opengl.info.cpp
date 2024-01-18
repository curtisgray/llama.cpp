#define GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX 0x9048
#define GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX 0x9049
//#define WGL_WGLEXT_PROTOTYPES
#ifdef _WIN32
#include <Windows.h>
#endif
#include <exception>
#include <GL/gl.h>
//#include <GL/wgl.h>
//#include <GL/glext.h>
#include "opengl.info.h"

namespace wingman::opengl {

	bool GetGpuMemory(long &total, long &free)
	{
		try {
			if (NVidiaMemoryInKb(total, free)) {
				return true;
			}
			if (AmdMemoryInKb(total, free)) {
				return true;
			}
		} catch (const std::exception &e) {
			// ignore
		}
		//try {
		//	if (AMDMemoryInKB(total, current)) {
		//		return true;
		//	}
		//} catch (const std::exception &e) {
		//	// ignore
		//}
		return false;
	}

	bool NVidiaMemoryInKb(long &total, long &free)
	{
		try {
			//glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX, &total);
			//glGetIntegerv(GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX, &current);
			GLint nTotalMemoryInKB = 0;
			glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX,
								   &nTotalMemoryInKB);
			total = nTotalMemoryInKB;
			GLint nCurAvailMemoryInKB = 0;
			glGetIntegerv(GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX,
								   &nCurAvailMemoryInKB);
			free = nCurAvailMemoryInKB;
			return true;
		} catch (const std::exception &e) {
			return false;
		}
		//GLint nTotalMemoryInKB = 0;
		//glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX,
		//					   &nTotalMemoryInKB);
		//total = nTotalMemoryInKB;
		//GLint nCurAvailMemoryInKB = 0;
		//glGetIntegerv(GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX,
		//					   &nCurAvailMemoryInKB);
		//current = nCurAvailMemoryInKB;
		//return true;
	}

	bool AmdMemoryInKb(long &total, long &free)
	{	// TODO: fix WGL errors during compile
		return false;
	//	try {
	//		GLuint uNoOfGPUs = wglGetGPUIDsAMD(0, 0);
	//		GLuint *uGPUIDs = new GLuint[uNoOfGPUs];
	//		wglGetGPUIDsAMD(uNoOfGPUs, uGPUIDs);
	//
	//		GLuint uTotalMemoryInMB = 0;
	//		wglGetGPUInfoAMD(uGPUIDs[0], WGL_GPU_RAM_AMD,
	//						GL_UNSIGNED_INT,
	//						sizeof(GLuint),
	//						&uTotalMemoryInMB);
	//		total = uTotalMemoryInMB;
	//
	//		GLint nCurAvailMemoryInKB = 0;
	//		glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI,
	//			&nCurAvailMemoryInKB);
	//		current = nCurAvailMemoryInKB;
	//		return true;
	//	} catch (const std::exception &e) {
	//		return false;
	//	}
	//	//GLuint uNoOfGPUs = wglGetGPUIDsAMD(0, 0);
	//	//GLuint *uGPUIDs = new GLuint[uNoOfGPUs];
	//	//wglGetGPUIDsAMD(uNoOfGPUs, uGPUIDs);
	//
	//	//GLuint uTotalMemoryInMB = 0;
	//	//wglGetGPUInfoAMD(uGPUIDs[0], WGL_GPU_RAM_AMD,
	//	//	GL_UNSIGNED_INT,
	//	//	sizeof(GLuint),
	//	//	&uTotalMemoryInMB);
	//
	//	//GLint nCurAvailMemoryInKB = 0;
	//	//glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI,
	//	//					   &nCurAvailMemoryInKB);
	}
}
