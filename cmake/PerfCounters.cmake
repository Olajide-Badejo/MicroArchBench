# cmake/PerfCounters.cmake
# Check whether perf_event_open(2) is available on the build host.
# Sets PERF_EVENT_AVAILABLE and checks for libnuma.

include(CheckCXXSourceCompiles)

# ---- perf_event_open availability ----
check_cxx_source_compiles("
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
int main() {
    struct perf_event_attr attr{};
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CACHE_MISSES;
    long fd = syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    (void)fd;
    return 0;
}
" PERF_EVENT_AVAILABLE)

if(PERF_EVENT_AVAILABLE)
    message(STATUS "PerfCounters: perf_event_open -- available")
else()
    message(STATUS "PerfCounters: perf_event_open -- NOT available (Linux-only; perf counters disabled)")
endif()

# ---- libnuma ----
if(BENCH_WITH_NUMA OR BENCH_WITH_NUMA STREQUAL "AUTO")
    find_library(NUMA_LIBRARY numa)
    find_path(NUMA_INCLUDE_DIR numa.h)
    if(NUMA_LIBRARY AND NUMA_INCLUDE_DIR)
        set(NUMA_FOUND TRUE CACHE INTERNAL "libnuma found")
        message(STATUS "PerfCounters: libnuma -- found at ${NUMA_LIBRARY}")
    else()
        set(NUMA_FOUND FALSE CACHE INTERNAL "")
        message(STATUS "PerfCounters: libnuma -- not found (NUMA pinning disabled)")
    endif()
endif()
