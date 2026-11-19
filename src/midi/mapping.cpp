#include "neurogenic/midi/mapping.hpp"

namespace neurogenic {
void MidiMapper::map_button(std::uint8_t channel, std::uint8_t note,
                            std::string id, Action action) {
    buttons_[static_cast<std::uint16_t>((channel << 8) | note)] = {
        std::move(id), std::move(action)};
}

bool MidiMapper::receive(const MidiMessage &message) noexcept {
    const auto type = message.status & 0xF0;
    if (type != 0x90 && type != 0x80)
        return false;
    if (!inbox_.push(message)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sequence_.fetch_add(1, std::memory_order_release);
    sequence_.notify_one();
    return true;
}

bool MidiMapper::dispatch_one() {
    MidiMessage message;
    if (!inbox_.pop(message))
        return false;
    const bool note_on = (message.status & 0xF0) == 0x90 && message.data2 != 0;
    if (note_event_handler_)
        note_event_handler_(message.data1, message.data2 / 127.0F, note_on);
    if (note_on && note_handler_)
        note_handler_(message.data1, message.data2 / 127.0F);
    const auto key = static_cast<std::uint16_t>(((message.status & 15) << 8) |
                                                message.data1);
    if (note_on)
        if (auto it = buttons_.find(key); it != buttons_.end())
            it->second.action();
    return true;
}

std::size_t MidiMapper::dispatch_pending() {
    std::size_t count{};
    while (dispatch_one())
        ++count;
    return count;
}
} // namespace neurogenic
