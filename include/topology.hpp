#pragma once
// include/topology.hpp
// Reads CPU topology from /proc/cpuinfo and /sys/devices/system/cpu/.
// Used to select appropriate PMU event codes and report hardware parameters.

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __linux__
#include <unistd.h>   // sysconf(_SC_NPROCESSORS_ONLN)
#endif

// ---------------------------------------------------------------------------
// CpuInfo
// Represents the essential topology visible to the benchmarks.
// ---------------------------------------------------------------------------

struct CpuInfo {
    std::string model_name;     // e.g. "Intel(R) Core(TM) i7-12700K"
    std::string vendor_id;      // "GenuineIntel", "AuthenticAMD", etc.
    int         cpu_family  = 0;
    int         model       = 0;
    int         stepping    = 0;
    int         num_cpus    = 0; // logical CPU count

    // Detected micro-architecture family (coarse-grained)
    enum class Arch { Unknown, IntelSkylake, IntelAlder, AMDZen3, AMDZen4, ARMCortexA72, ARMOther };
    Arch arch = Arch::Unknown;
};

// Reads /proc/cpuinfo and returns a CpuInfo struct.
// Returns a default-constructed struct on non-Linux platforms.
inline CpuInfo read_cpu_info() {
    CpuInfo info{};

#ifdef __linux__
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (!f) return info;

    char line[256]{};
    while (fgets(line, sizeof(line), f)) {
        auto field = [&](const char* key) -> const char* {
            if (strncmp(line, key, strlen(key)) == 0) {
                const char* p = strchr(line, ':');
                if (p) {
                    ++p;
                    while (*p == ' ' || *p == '\t') ++p;
                    return p;
                }
            }
            return nullptr;
        };

        if (const char* v = field("model name"))
            info.model_name = std::string(v);
        if (const char* v = field("vendor_id"))
            info.vendor_id = std::string(v);
        if (const char* v = field("cpu family"))
            info.cpu_family = atoi(v);
        if (const char* v = field("model\t"))
            info.model = atoi(v);
        if (const char* v = field("stepping"))
            info.stepping = atoi(v);
        if (const char* v = field("processor"))
            info.num_cpus = atoi(v) + 1; // last "processor" entry = count-1
    }
    fclose(f);

    // Strip trailing newline from string fields
    for (auto* s : {&info.model_name, &info.vendor_id}) {
        if (!s->empty() && s->back() == '\n')
            s->pop_back();
    }

    // Classify micro-architecture
    if (info.vendor_id.find("Intel") != std::string::npos) {
        // Family 6 covers all modern Intel client/server CPUs
        if (info.cpu_family == 6) {
            if (info.model == 0x97 || info.model == 0x9A)
                info.arch = CpuInfo::Arch::IntelAlder;  // Alder Lake
            else if (info.model >= 0x55 && info.model <= 0x8F)
                info.arch = CpuInfo::Arch::IntelSkylake; // Skylake/Cascade Lake/Ice Lake
            else
                info.arch = CpuInfo::Arch::IntelSkylake; // safe fallback
        }
    } else if (info.vendor_id.find("AMD") != std::string::npos) {
        if (info.cpu_family == 25)
            info.arch = CpuInfo::Arch::AMDZen3; // Zen 3 / Zen 4 both family 25 or 26
        else
            info.arch = CpuInfo::Arch::AMDZen3; // fallback
    } else if (info.vendor_id.find("ARM") != std::string::npos ||
               info.model_name.find("Cortex") != std::string::npos) {
        if (info.model_name.find("A72") != std::string::npos)
            info.arch = CpuInfo::Arch::ARMCortexA72;
        else
            info.arch = CpuInfo::Arch::ARMOther;
    }
#endif

    return info;
}

// ---------------------------------------------------------------------------
// PhysicalCores
// Returns the number of physical (non-HT) cores on cpu0's socket.
// Falls back to logical count if sysfs info is unavailable.
// ---------------------------------------------------------------------------

inline int physical_core_count() {
#ifdef __linux__
    // /sys/devices/system/cpu/cpu0/topology/core_id lists unique cores
    // A simpler approach: count unique entries in core_id for all cpus
    std::vector<int> seen_cores;
    for (int cpu = 0; cpu < 256; ++cpu) {
        char path[128]{};
        snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
        FILE* f = fopen(path, "r");
        if (!f) break;
        int id = -1;
        if (fscanf(f, "%d", &id) == 1) {
            bool found = false;
            for (int c : seen_cores) if (c == id) { found = true; break; }
            if (!found) seen_cores.push_back(id);
        }
        fclose(f);
    }
    if (!seen_cores.empty()) return static_cast<int>(seen_cores.size());
#endif
    // Fallback: use logical count
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}
