#pragma once
#include "neurogenic/sequencer/pattern.hpp"
#include <filesystem>
namespace neurogenic { struct Project { static constexpr unsigned format_version=1; Pattern pattern; }; void save_project(const Project&,const std::filesystem::path&);Project load_project(const std::filesystem::path&); }
