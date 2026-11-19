#pragma once
#include "neurogenic/core/spsc_queue.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace neurogenic {
struct MidiMessage {
    std::uint8_t status{}, data1{}, data2{};
    std::uint64_t timestamp_ns{};
};

// One MIDI transport producer enqueues messages; one control thread dispatches
// them. Mapping configuration is completed before the dispatcher starts.
class MidiMapper {
  public:
    using Action = std::function<void()>;
    using NoteHandler = std::function<void(std::uint8_t, float)>;
    using NoteEventHandler = std::function<void(std::uint8_t, float, bool)>;
    void map_button(std::uint8_t channel, std::uint8_t note, std::string id,
                    Action action);
    void set_note_handler(NoteHandler handler) {
        note_handler_ = std::move(handler);
    }
    void set_note_event_handler(NoteEventHandler handler) {
        note_event_handler_ = std::move(handler);
    }
    bool receive(const MidiMessage &) noexcept;
    bool dispatch_one();
    std::size_t dispatch_pending();
    [[nodiscard]] std::uint64_t dropped_messages() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    friend class ControlDispatcher;
    struct Entry {
        std::string id;
        Action action;
    };
    SpscQueue<MidiMessage, 257> inbox_;
    std::unordered_map<std::uint16_t, Entry> buttons_;
    NoteHandler note_handler_;
    NoteEventHandler note_event_handler_;
    std::atomic<std::uint64_t> dropped_{}, sequence_{};
};
} // namespace neurogenic
