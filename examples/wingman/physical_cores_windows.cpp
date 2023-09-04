#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#if defined(_MSC_VER)
#pragma warning(disable : 4244 4267) // possible loss of data
#endif

#if defined(_WIN32)

size_t NumberOfPhysicalCores() noexcept
{

    DWORD length = 0;
    const BOOL result_first = GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[length]);
    const PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get());

    const BOOL result_second = GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length);
    assert(result_second != FALSE);

    size_t nb_physical_cores = 0;
    size_t offset = 0;
    do {
        const PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX current_info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get() + offset);
        offset += current_info->Size;
        ++nb_physical_cores;
    } while (offset < length);

    return nb_physical_cores;
}
#endif
