#pragma once
#include "neurogenic/midi/mapping.hpp"
#include <memory>
#include <string>
#include <vector>
namespace neurogenic { class MidiInput { public:explicit MidiInput(MidiMapper&);~MidiInput();MidiInput(const MidiInput&)=delete;MidiInput& operator=(const MidiInput&)=delete;std::string backend() const;std::vector<std::string> inputs() const;bool open(const std::string& selector,std::string& error);private:struct Impl;std::unique_ptr<Impl> impl_;}; }
