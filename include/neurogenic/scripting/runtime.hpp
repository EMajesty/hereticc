#pragma once
#include "neurogenic/sequencer/pattern.hpp"
#include <functional>
#include <string>
namespace neurogenic { class ScriptRuntime { public: using Submit=std::function<void(Pattern)>; explicit ScriptRuntime(Submit submit):submit_(std::move(submit)){} bool execute(const std::string& source,std::string& error) noexcept; static constexpr auto api_version="neurogenic.v1"; private:Submit submit_; }; }
