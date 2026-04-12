#pragma once
// include/perf_event.hpp
// RAII wrapper around the Linux perf_event_open(2) syscall.
// Only compiled when BENCH_HAS_PERF is defined by CMake.

#ifdef BENCH_HAS_PERF

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// PerfCounter
// Wraps a single perf_event_open file descriptor. Supports both simple
// read format and the scaled read format needed when PMU multiplexing occurs.
// ---------------------------------------------------------------------------

class PerfCounter {
public:
    // Construct and open the counter. Does not start counting yet.
    // type   -- e.g. PERF_TYPE_HARDWARE
    // config -- e.g. PERF_COUNT_HW_CACHE_MISSES
    // group_fd -- pass -1 for standalone, or a leader fd to create a group
    explicit PerfCounter(uint32_t type, uint64_t config, int group_fd = -1) {
        perf_event_attr attr{};
        attr.type           = type;
        attr.size           = sizeof(attr);
        attr.config         = config;
        attr.disabled       = 1;   // start disabled; caller calls enable()
        attr.exclude_kernel = 1;   // count user-space only
        attr.exclude_hv     = 1;   // exclude hypervisor
        attr.inherit        = 0;   // do not inherit across fork
        attr.pinned         = 0;   // allow multiplexing when needed

        // Request time_enabled / time_running for scaling calculation
        attr.read_format =
            PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

        fd_ = static_cast<int>(
            syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, 0));

        if (fd_ < 0) {
            throw std::system_error(
                errno, std::system_category(),
                "perf_event_open failed (check kernel.perf_event_paranoid)");
        }
    }

    ~PerfCounter() {
        if (fd_ >= 0) close(fd_);
    }

    // Non-copyable
    PerfCounter(const PerfCounter&)            = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;

    // Move-constructible
    PerfCounter(PerfCounter&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

    void enable()  { ioctl(fd_, PERF_EVENT_IOC_ENABLE,  0); }
    void disable() { ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0); }
    void reset()   { ioctl(fd_, PERF_EVENT_IOC_RESET,   0); }

    int fd() const { return fd_; }

    // Raw read of the counter value only.
    // The kernel writes the full read_format struct (value + time_enabled +
    // time_running = 24 bytes) even for a "simple" read because those format
    // flags are baked into attr at construction time. We read all three fields
    // and return only the value, discarding the time fields.
    // Returns 0 on error (short read or fd invalid).
    uint64_t read_raw() const {
        struct { uint64_t value; uint64_t time_enabled; uint64_t time_running; } data{};
        if (::read(fd_, &data, sizeof(data)) != static_cast<ssize_t>(sizeof(data)))
            return 0;
        return data.value;
    }

    // Scaled read -- accounts for PMU multiplexing.
    // If time_running < time_enabled the counter was not scheduled the whole
    // time; we scale linearly. The result is marked as approximate.
    struct ScaledValue {
        uint64_t value;
        bool     multiplexed;  // true when time_running < time_enabled
    };

    ScaledValue read_scaled() const {
        struct {
            uint64_t value;
            uint64_t time_enabled;
            uint64_t time_running;
        } data{};

        if (::read(fd_, &data, sizeof(data)) != static_cast<ssize_t>(sizeof(data)))
            return {0, false};

        if (data.time_running == 0)
            return {0, true};

        bool muxed = (data.time_running < data.time_enabled);
        uint64_t scaled = muxed
            ? static_cast<uint64_t>(
                static_cast<double>(data.value) *
                static_cast<double>(data.time_enabled) /
                static_cast<double>(data.time_running))
            : data.value;

        return {scaled, muxed};
    }

private:
    int fd_ = -1;
};

// ---------------------------------------------------------------------------
// Common hardware event shortcuts
// These are generic names; not every PMU supports all of them.
// ---------------------------------------------------------------------------

namespace PerfEvents {
    // Hardware generic events (PERF_TYPE_HARDWARE)
    inline PerfCounter cache_misses() {
        return PerfCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
    }
    inline PerfCounter instructions() {
        return PerfCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
    }
    inline PerfCounter cpu_cycles() {
        return PerfCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
    }
    inline PerfCounter branch_misses() {
        return PerfCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);
    }
    // Hardware cache events (PERF_TYPE_HW_CACHE)
    // LLC load misses: (LLC | READ | MISS)
    inline PerfCounter llc_load_misses() {
        uint64_t config =
            (PERF_COUNT_HW_CACHE_LL) |
            (PERF_COUNT_HW_CACHE_OP_READ   << 8) |
            (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        return PerfCounter(PERF_TYPE_HW_CACHE, config);
    }
} // namespace PerfEvents

// ---------------------------------------------------------------------------
// ScopedPerfSession
// Convenience RAII scope that enables a set of counters on entry and
// disables + reads them on exit.
// ---------------------------------------------------------------------------

struct PerfReading {
    std::string            name;
    PerfCounter::ScaledValue value;
};

class ScopedPerfSession {
public:
    void add(std::string name, uint32_t type, uint64_t config) {
        names_.push_back(std::move(name));
        counters_.emplace_back(type, config);
    }

    void start() {
        for (auto& c : counters_) { c.reset(); c.enable(); }
    }

    std::vector<PerfReading> stop() {
        for (auto& c : counters_) c.disable();
        std::vector<PerfReading> out;
        for (std::size_t i = 0; i < counters_.size(); ++i)
            out.push_back({names_[i], counters_[i].read_scaled()});
        return out;
    }

private:
    std::vector<std::string>  names_;
    std::vector<PerfCounter>  counters_;
};

#endif // BENCH_HAS_PERF
