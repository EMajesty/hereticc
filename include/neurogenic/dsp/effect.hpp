#pragma once
#include <span>
namespace neurogenic { class Effect { public: virtual ~Effect()=default; virtual void process(std::span<float> left, std::span<float> right) noexcept=0; }; }
