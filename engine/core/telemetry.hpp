#pragma once
#include <cstdint>
#ifdef _WIN32
#define NOMINMAX
#include <psapi.h>
#include <windows.h>
#else
#include <sys/resource.h>
#endif
namespace vc {
inline uint64_t peak_rss_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.PeakWorkingSetSize;
    return 0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage))
        return 0;
#ifdef __APPLE__
    return uint64_t(usage.ru_maxrss);
#else
    return uint64_t(usage.ru_maxrss) * 1024;
#endif
#endif
}
} // namespace vc
