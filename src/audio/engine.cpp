#include "neurogenic/audio/engine.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace neurogenic {
AudioEngine::AudioEngine(std::uint32_t rate,std::size_t voice_capacity)
    :rate_(rate),scheduler_(rate,120.0),voices_(voice_capacity){
  if(voice_capacity==0)throw std::invalid_argument("voice capacity must be positive");
}

void AudioEngine::set_sample(Sample sample){
  if(processing_started_.load(std::memory_order_acquire))
    throw std::logic_error("sample must be installed before audio processing starts");
  sample_=std::move(sample);
}

void AudioEngine::trigger(std::uint8_t note,float velocity,std::uint32_t frame_offset) noexcept {
  const Trigger event{note,velocity,frame_offset,next_order_.fetch_add(1,std::memory_order_relaxed)};
  if(!triggers_.push(event))dropped_triggers_.fetch_add(1,std::memory_order_relaxed);
}

bool AudioEngine::queue_pattern(const Pattern& pattern) noexcept {
  if(!valid_pattern(pattern))return false;
  if(!patterns_.push(pattern)){dropped_patterns_.fetch_add(1,std::memory_order_relaxed);return false;}
  return true;
}

void AudioEngine::start_voice(float velocity) noexcept {
  auto selected=voices_.size();
  for(std::size_t i=0;i<voices_.size();++i)if(!voices_[i].active){selected=i;break;}
  if(selected==voices_.size()){
    selected=0;
    for(std::size_t i=1;i<voices_.size();++i)
      if(voices_[i].age<voices_[selected].age)selected=i;
    stolen_voices_.fetch_add(1,std::memory_order_relaxed);
  }
  voices_[selected]={0,velocity,next_voice_age_++,true};
}

void AudioEngine::process(std::span<float> left,std::span<float> right) noexcept {
  processing_started_.store(true,std::memory_order_release);
  const auto frames=std::min(left.size(),right.size());
  std::fill(left.begin(),left.end(),0);std::fill(right.begin(),right.end(),0);

  Pattern pattern;
  while(patterns_.pop(pattern))scheduler_.request_pattern(pattern);

  Trigger incoming;
  while(pending_trigger_count_<pending_triggers_.size()&&triggers_.pop(incoming))
    pending_triggers_[pending_trigger_count_++]=incoming;
  for(std::size_t i=1;i<pending_trigger_count_;++i){
    auto value=pending_triggers_[i];auto j=i;
    while(j>0&&(pending_triggers_[j-1].frame_offset>value.frame_offset||
          (pending_triggers_[j-1].frame_offset==value.frame_offset&&pending_triggers_[j-1].order>value.order))){
      pending_triggers_[j]=pending_triggers_[j-1];--j;
    }
    pending_triggers_[j]=value;
  }

  std::array<ScheduledEvent,64> scheduled{};
  const auto scheduled_count=scheduler_.schedule(static_cast<std::uint32_t>(frames),scheduled);
  published_frame_.store(scheduler_.frame_position(),std::memory_order_release);
  std::size_t external_index{},scheduled_index{};

  for(std::size_t frame=0;frame<frames;++frame){
    while(external_index<pending_trigger_count_&&pending_triggers_[external_index].frame_offset==frame)
      start_voice(pending_triggers_[external_index++].velocity);
    while(scheduled_index<scheduled_count&&scheduled[scheduled_index].frame==frame)
      start_voice(scheduled[scheduled_index++].velocity);
    for(auto& voice:voices_){
      if(!voice.active)continue;
      if(!sample_.channels||voice.cursor+sample_.channels>sample_.interleaved.size()){voice.active=false;continue;}
      left[frame]+=sample_.interleaved[voice.cursor]*voice.gain;
      right[frame]+=sample_.interleaved[voice.cursor+(sample_.channels>1?1:0)]*voice.gain;
      voice.cursor+=sample_.channels;
      if(voice.cursor>=sample_.interleaved.size())voice.active=false;
    }
  }

  const auto remaining=pending_trigger_count_-external_index;
  for(std::size_t i=0;i<remaining;++i){
    pending_triggers_[i]=pending_triggers_[external_index+i];
    pending_triggers_[i].frame_offset-=static_cast<std::uint32_t>(frames);
  }
  pending_trigger_count_=remaining;
  effect_.process(left.first(frames),right.first(frames));
}

void AudioEngine::render_wav(const std::filesystem::path& path,std::uint64_t frames){
  std::ofstream output(path,std::ios::binary);
  auto write16=[&](std::uint16_t value){output.write(reinterpret_cast<char*>(&value),2);};
  auto write32=[&](std::uint32_t value){output.write(reinterpret_cast<char*>(&value),4);};
  output.write("RIFF",4);write32(36+frames*4);output.write("WAVEfmt ",8);write32(16);write16(1);write16(2);
  write32(rate_);write32(rate_*4);write16(4);write16(16);output.write("data",4);write32(frames*4);
  std::array<float,256> left{},right{};
  for(std::uint64_t done=0;done<frames;){
    const auto count=std::min<std::uint64_t>(left.size(),frames-done);process({left.data(),count},{right.data(),count});
    for(std::size_t i=0;i<count;++i){
      auto convert=[](float value){return static_cast<std::int16_t>(std::clamp(value,-1.0F,1.0F)*32767);};
      auto l=convert(left[i]),r=convert(right[i]);output.write(reinterpret_cast<char*>(&l),2);output.write(reinterpret_cast<char*>(&r),2);
    }done+=count;
  }
}
}
