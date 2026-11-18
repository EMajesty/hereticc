#pragma once
#include "neurogenic/sequencer/pattern.hpp"
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
namespace neurogenic { class ScriptRuntime { public: using Submit=std::function<bool(Pattern)>;using Position=std::function<std::uint64_t()>;ScriptRuntime(Submit,Position);~ScriptRuntime();ScriptRuntime(const ScriptRuntime&)=delete;ScriptRuntime& operator=(const ScriptRuntime&)=delete;bool execute(const std::string&,std::string&) noexcept;bool execute_file(const std::filesystem::path&,std::string&) noexcept;static constexpr auto api_version="neurogenic.v1";private:struct Impl;std::unique_ptr<Impl> impl_; }; }
