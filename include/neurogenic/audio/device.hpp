#pragma once
#include <memory>
#include <string>
#include <vector>
namespace neurogenic { class AudioEngine; class AudioDevice { public:explicit AudioDevice(AudioEngine&,bool force_null=false);~AudioDevice();AudioDevice(const AudioDevice&)=delete;AudioDevice& operator=(const AudioDevice&)=delete;std::string backend() const;std::vector<std::string> outputs() const;bool start(const std::string& selector,std::string& error);void stop() noexcept;private:struct Impl;std::unique_ptr<Impl> impl_;}; }
