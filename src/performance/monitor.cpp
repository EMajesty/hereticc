#include "neurogenic/performance/monitor.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#ifdef __linux__
#include <unistd.h>
#endif

namespace neurogenic {
double cpu_percent(std::uint64_t work, std::uint64_t wall) noexcept {
    return wall == 0
               ? 0.0
               : 100.0 * static_cast<double>(work) / static_cast<double>(wall);
}
std::string performance_json(const PerformanceSnapshot &value) {
    std::ostringstream out;
    out << "{\"schema\":\"neurogenic.performance.v1\",\"generation\":"
        << value.generation
        << ",\"timestamp_ms\":" << value.monotonic_milliseconds
        << ",\"audio\":{\"available\":"
        << (value.audio.available ? "true" : "false")
        << ",\"current_ns\":" << value.audio.current_nanoseconds
        << ",\"average_ns\":" << value.audio.average_nanoseconds
        << ",\"peak_ns\":" << value.audio.peak_nanoseconds
        << ",\"deadline_ns\":" << value.audio.deadline_nanoseconds
        << ",\"load_percent\":" << value.audio.current_load_percent
        << ",\"callback_count\":" << value.audio.callback_count
        << ",\"deadline_overruns\":" << value.audio.deadline_overruns
        << "},\"engine\":{\"sample_rate\":" << value.engine.sample_rate
        << ",\"buffer_frames\":" << value.engine.buffer_frames
        << ",\"active_voices\":" << value.engine.active_voices
        << ",\"maximum_voices\":" << value.engine.maximum_voices
        << ",\"timeline_publication_failures\":"
        << value.engine.dropped_timelines
        << ",\"active_synth_voices\":" << value.engine.active_synth_voices
        << ",\"maximum_synth_voices\":" << value.engine.maximum_synth_voices
        << ",\"synth_voice_steals\":" << value.engine.synth_stolen_voices
        << ",\"recording_frames\":"
        << value.engine.recording_captured_frames
        << ",\"recording_capacity_frames\":"
        << value.engine.recording_capacity_frames
        << ",\"recording_dropped_frames\":"
        << value.engine.recording_dropped_frames
        << ",\"recording_overflows\":"
        << value.engine.recording_overflows
        << ",\"recording_finalization_us\":"
        << value.engine.recording_finalization_microseconds
        << ",\"recording_memory_bytes\":"
        << value.engine.recording_memory_bytes
        << ",\"queue_overflows\":"
        << (value.engine.dropped_triggers + value.engine.dropped_patterns +
            value.engine.dropped_timelines + value.engine.rejected_commands)
        << "},\"system\":{\"process_cpu_available\":"
        << (value.system.process_cpu_available ? "true" : "false")
        << ",\"process_cpu_percent\":" << value.system.process_cpu_percent
        << ",\"resident_memory_available\":"
        << (value.system.resident_memory_available ? "true" : "false")
        << ",\"resident_memory_bytes\":" << value.system.resident_memory_bytes
        << ",\"total_memory_available\":"
        << (value.system.total_memory_available ? "true" : "false")
        << ",\"total_memory_bytes\":" << value.system.total_memory_bytes
        << "}}";
    return out.str();
}
void saturating_increment(std::atomic<std::uint64_t> &value) noexcept {
    auto current = value.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max() &&
           !value.compare_exchange_weak(current, current + 1,
                                        std::memory_order_relaxed)) {
    }
}
void CallbackMonitor::record(std::uint64_t elapsed, std::uint32_t frames,
                             std::uint32_t rate, std::size_t voices) noexcept {
    if (!enabled_.load(std::memory_order_relaxed))
        return;
    const auto deadline = rate == 0 ? 0
                                    : static_cast<std::uint64_t>(frames) *
                                          1'000'000'000ULL / rate;
    current_.store(elapsed, std::memory_order_relaxed);
    deadline_.store(deadline, std::memory_order_relaxed);
    active_voices_.store(voices, std::memory_order_relaxed);
    auto average = average_.load(std::memory_order_relaxed);
    average_.store(average == 0 ? elapsed : (average * 63 + elapsed) / 64,
                   std::memory_order_relaxed);
    auto peak = peak_.load(std::memory_order_relaxed);
    while (elapsed > peak && !peak_.compare_exchange_weak(
                                 peak, elapsed, std::memory_order_relaxed)) {
    }
    saturating_increment(count_);
    if (deadline != 0 && elapsed > deadline)
        saturating_increment(overruns_);
}
AudioCallbackStatistics CallbackMonitor::snapshot() const noexcept {
    AudioCallbackStatistics result;
    result.available = enabled();
    result.current_nanoseconds = current_.load(std::memory_order_relaxed);
    result.average_nanoseconds = average_.load(std::memory_order_relaxed);
    result.peak_nanoseconds = peak_.load(std::memory_order_relaxed);
    result.deadline_nanoseconds = deadline_.load(std::memory_order_relaxed);
    result.callback_count = count_.load(std::memory_order_relaxed);
    result.deadline_overruns = overruns_.load(std::memory_order_relaxed);
    if (result.deadline_nanoseconds) {
        const auto load = [&](std::uint64_t value) {
            return 100.0 * static_cast<double>(value) /
                   static_cast<double>(result.deadline_nanoseconds);
        };
        result.current_load_percent = load(result.current_nanoseconds);
        result.average_load_percent = load(result.average_nanoseconds);
        result.peak_load_percent = load(result.peak_nanoseconds);
    }
    return result;
}

namespace {
constexpr std::uint8_t process_cpu_bit = 1, resident_bit = 2, total_bit = 4,
                       system_cpu_bit = 8;
struct Reading {
    std::uint64_t process_ticks{}, total_ticks{}, idle_ticks{}, resident{},
        total{};
    std::uint8_t available{};
};
Reading read_system() noexcept {
    Reading result;
#ifdef __linux__
    std::ifstream stat("/proc/self/stat");
    std::string line;
    if (std::getline(stat, line)) {
        const auto close = line.rfind(')');
        if (close != std::string::npos) {
            std::istringstream fields(line.substr(close + 2));
            std::string value;
            for (int field = 3; field <= 15 && fields >> value; ++field)
                if (field == 14 || field == 15) {
                    std::uint64_t parsed{};
                    const auto conversion = std::from_chars(
                        value.data(), value.data() + value.size(), parsed);
                    if (conversion.ec == std::errc{})
                        result.process_ticks += parsed;
                }
            result.available |= process_cpu_bit;
        }
    }
    std::ifstream process_status("/proc/self/status");
    std::string key;
    while (process_status >> key) {
        if (key == "VmRSS:") {
            std::uint64_t kb{};
            process_status >> kb;
            result.resident = kb * 1024;
            result.available |= resident_bit;
            break;
        }
        std::getline(process_status, line);
    }
    std::ifstream mem("/proc/meminfo");
    while (mem >> key) {
        if (key == "MemTotal:") {
            std::uint64_t kb{};
            mem >> kb;
            result.total = kb * 1024;
            result.available |= total_bit;
            break;
        }
        std::getline(mem, line);
    }
    std::ifstream system("/proc/stat");
    if (system >> key && key == "cpu") {
        std::uint64_t user{}, nice{}, sys{}, idle{}, wait{}, irq{}, soft{},
            steal{};
        if (system >> user >> nice >> sys >> idle >> wait >> irq >> soft >>
            steal) {
            result.idle_ticks = idle + wait;
            result.total_ticks =
                user + nice + sys + idle + wait + irq + soft + steal;
            result.available |= system_cpu_bit;
        }
    }
#endif
    return result;
}
} // namespace
SystemStatistics sample_system_now() noexcept {
    const auto value = read_system();
    SystemStatistics result;
    result.resident_memory_available = value.available & resident_bit;
    result.total_memory_available = value.available & total_bit;
    result.resident_memory_bytes = value.resident;
    result.total_memory_bytes = value.total;
    return result;
}
SystemMonitor::SystemMonitor(std::chrono::milliseconds interval)
    : interval_(interval) {
    const auto initial = read_system();
    resident_.store(initial.resident);
    total_.store(initial.total);
    availability_.store(initial.available &
                        static_cast<std::uint8_t>(resident_bit | total_bit));
    timestamp_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
    generation_.store(1);
    worker_ = std::thread(&SystemMonitor::run, this);
}
SystemMonitor::~SystemMonitor() {
    stopping_.store(true, std::memory_order_release);
    wake_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
SystemStatistics SystemMonitor::snapshot() const noexcept {
    const auto flags = availability_.load(std::memory_order_acquire);
    SystemStatistics result;
    result.process_cpu_available = flags & process_cpu_bit;
    result.resident_memory_available = flags & resident_bit;
    result.total_memory_available = flags & total_bit;
    result.system_cpu_available = flags & system_cpu_bit;
    result.process_cpu_percent =
        process_cpu_milli_.load(std::memory_order_relaxed) / 1000.0;
    result.system_cpu_percent =
        system_cpu_milli_.load(std::memory_order_relaxed) / 1000.0;
    result.resident_memory_bytes = resident_.load(std::memory_order_relaxed);
    result.total_memory_bytes = total_.load(std::memory_order_relaxed);
    return result;
}
void SystemMonitor::run() noexcept {
    auto previous = read_system();
    auto before = std::chrono::steady_clock::now();
    while (!stopping_.load(std::memory_order_acquire)) {
        {
            std::unique_lock lock(wait_mutex_);
            wake_.wait_for(lock, interval_, [&] {
                return stopping_.load(std::memory_order_acquire);
            });
        }
        if (stopping_.load(std::memory_order_acquire))
            break;
        const auto now = std::chrono::steady_clock::now();
        const auto current = read_system();
        auto flags = current.available;
#ifdef __linux__
        const auto ticks =
            static_cast<std::uint64_t>(std::max<long>(1, sysconf(_SC_CLK_TCK)));
        if ((flags & process_cpu_bit) &&
            (previous.available & process_cpu_bit)) {
            const auto delta = current.process_ticks - previous.process_ticks;
            const auto wall =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now -
                                                                     before)
                    .count();
            process_cpu_milli_.store(static_cast<std::uint64_t>(
                cpu_percent(delta * 1'000'000'000ULL / ticks, wall) * 1000));
        } else
            flags &= static_cast<std::uint8_t>(~process_cpu_bit);
#endif
        if ((flags & system_cpu_bit) && (previous.available & system_cpu_bit)) {
            const auto total = current.total_ticks - previous.total_ticks;
            const auto idle = current.idle_ticks - previous.idle_ticks;
            system_cpu_milli_.store(total ? 100'000ULL * (total - idle) / total
                                          : 0);
        } else
            flags &= static_cast<std::uint8_t>(~system_cpu_bit);
        resident_.store(current.resident);
        total_.store(current.total);
        availability_.store(flags, std::memory_order_release);
        timestamp_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count(),
            std::memory_order_release);
        saturating_increment(generation_);
        previous = current;
        before = now;
    }
}
} // namespace neurogenic
