#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace neurogenic {
struct NoteIdentity {
    std::uint64_t track{}, event{}, occurrence{};
    friend bool operator==(const NoteIdentity &,
                           const NoteIdentity &) = default;
};
struct InstrumentState {
    std::size_t active_voices{}, maximum_voices{};
    std::uint64_t stolen_voices{}, processed_nanoseconds{};
};
class Instrument {
  public:
    virtual ~Instrument() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual void note_on(std::uint8_t, float, NoteIdentity = {}) noexcept = 0;
    virtual void note_off(std::uint8_t, NoteIdentity = {}) noexcept = 0;
    virtual bool set_parameter(std::uint32_t, float) noexcept = 0;
    virtual void process(std::span<float>, std::span<float>) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void set_sample_rate(std::uint32_t) noexcept = 0;
    virtual InstrumentState state() const noexcept = 0;
};
} // namespace neurogenic
