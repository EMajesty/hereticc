#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
namespace neurogenic {
struct MidiMessage { std::uint8_t status{}, data1{}, data2{}; std::uint64_t timestamp_ns{}; };
class MidiMapper { public: using Action=std::function<void()>; void map_button(std::uint8_t channel,std::uint8_t note,std::string id,Action); bool receive(const MidiMessage&); private: struct Entry{std::string id;Action action;};std::unordered_map<std::uint16_t,Entry> buttons_; };
}
