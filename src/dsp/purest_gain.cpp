#include "neurogenic/dsp/purest_gain.hpp"
#include <algorithm>
namespace neurogenic { void PurestGain::set_gain(float v) noexcept { target_=std::clamp(v,0.0F,2.0F); } void PurestGain::process(std::span<float> l,std::span<float> r) noexcept { auto n=std::min(l.size(),r.size()); for(std::size_t i=0;i<n;++i){ current_ += (target_-current_)*0.001F; l[i]*=current_; r[i]*=current_; } } }
