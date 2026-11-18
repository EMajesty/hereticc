#include "neurogenic/audio/engine.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
namespace neurogenic {
AudioEngine::AudioEngine(std::uint32_t rate):rate_(rate),scheduler_(rate,120.0){}
void AudioEngine::set_sample(Sample s){ sample_=std::move(s); cursor_=sample_.interleaved.size(); }
void AudioEngine::trigger(std::uint8_t note,float velocity) noexcept { trigger_={note,velocity}; trigger_pending_.store(true,std::memory_order_release); }
void AudioEngine::process(std::span<float> l,std::span<float> r) noexcept {
 std::fill(l.begin(),l.end(),0); std::fill(r.begin(),r.end(),0); std::array<ScheduledEvent,64> events{}; auto n=scheduler_.schedule(static_cast<std::uint32_t>(l.size()),events); std::size_t ei=0;
 for(std::size_t i=0;i<l.size();++i){ if(trigger_pending_.exchange(false,std::memory_order_acq_rel)){cursor_=0;voice_gain_=trigger_.velocity;} while(ei<n&&events[ei].frame==i){cursor_=0;voice_gain_=events[ei++].velocity;}
   if(sample_.channels && cursor_+sample_.channels<=sample_.interleaved.size()){ l[i]=sample_.interleaved[cursor_]*voice_gain_; r[i]=sample_.interleaved[cursor_+(sample_.channels>1?1:0)]*voice_gain_; cursor_+=sample_.channels; }} effect_.process(l,r);
}
void AudioEngine::render_wav(const std::filesystem::path& path,std::uint64_t frames){ std::ofstream o(path,std::ios::binary); auto w16=[&](std::uint16_t v){o.write(reinterpret_cast<char*>(&v),2);}; auto w32=[&](std::uint32_t v){o.write(reinterpret_cast<char*>(&v),4);}; o.write("RIFF",4);w32(36+frames*4);o.write("WAVEfmt ",8);w32(16);w16(1);w16(2);w32(rate_);w32(rate_*4);w16(4);w16(16);o.write("data",4);w32(frames*4); std::array<float,256> l{},r{}; for(std::uint64_t done=0;done<frames;){auto count=std::min<std::uint64_t>(l.size(),frames-done);process({l.data(),count},{r.data(),count});for(std::size_t i=0;i<count;++i){auto cv=[](float x){return static_cast<std::int16_t>(std::clamp(x,-1.0F,1.0F)*32767);};auto a=cv(l[i]),b=cv(r[i]);o.write(reinterpret_cast<char*>(&a),2);o.write(reinterpret_cast<char*>(&b),2);}done+=count;}}
}
