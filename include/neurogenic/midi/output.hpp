#pragma once
#include "neurogenic/midi/routing.hpp"
#include <memory>
#include <string>
#include <vector>
namespace neurogenic {
struct MidiPortInfo {
    MidiPortId id{};
    std::string name;
    bool connected{};
};
class MidiOutput {
  public:
    MidiOutput();
    ~MidiOutput();
    MidiOutput(const MidiOutput &) = delete;
    MidiOutput &operator=(const MidiOutput &) = delete;
    [[nodiscard]] std::string backend() const;
    [[nodiscard]] std::vector<MidiPortInfo> outputs() const;
    bool connect(MidiPortId, const std::string &selector, std::string &error);
    void disconnect(MidiPortId) noexcept;
    bool send(const MidiPacket &, std::string &error) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace neurogenic
