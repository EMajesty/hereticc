#include "neurogenic/audio/engine.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace neurogenic {
AudioEngine::AudioEngine(std::uint32_t rate,std::size_t voice_capacity,std::size_t event_staging_capacity)
    :rate_(rate),scheduler_(rate,120.0),timeline_scheduler_(rate),voices_(voice_capacity),legacy_events_(event_staging_capacity),timeline_events_(event_staging_capacity){
  if(rate==0||voice_capacity==0||event_staging_capacity==0)throw std::invalid_argument("sample rate, voice capacity, and event staging capacity must be positive");
  note_slots_.fill(invalid_sample_slot);note_sample_indices_.fill(std::numeric_limits<std::size_t>::max());
  for(std::size_t i=0;i<default_note_bank_size;++i)note_slots_[36+i]=SampleSlotId{static_cast<std::uint32_t>(i)};
}

AudioEngine::~AudioEngine(){
  reclaim_retired_plans();PlaybackPlan* plan{};while(timelines_.pop(plan))delete plan;delete active_timeline_;
}

void AudioEngine::set_sample(Sample sample){
  set_sample_slot(SampleSlotId{0},std::move(sample));
  note_slots_.fill(SampleSlotId{0});note_sample_indices_.fill(find_slot(SampleSlotId{0}));
}

std::size_t AudioEngine::find_slot(SampleSlotId id) const noexcept{
  for(std::size_t i=0;i<samples_.size();++i)if(samples_[i].id==id)return i;
  return std::numeric_limits<std::size_t>::max();
}

bool AudioEngine::has_sample_slot(SampleSlotId id) const noexcept{return find_slot(id)!=std::numeric_limits<std::size_t>::max();}

void AudioEngine::set_sample_slot(SampleSlotId id,Sample sample){
  if(id==invalid_sample_slot)throw std::invalid_argument("invalid sample slot identifier");
  if(processing_started_.load(std::memory_order_acquire))throw std::logic_error("sample slots must be installed before audio processing starts");
  if(sample.sample_rate!=rate_)throw std::invalid_argument("sample rate does not match engine rate (resampling is not implemented)");
  if((sample.channels!=1&&sample.channels!=2)||sample.interleaved.empty()||sample.interleaved.size()%sample.channels!=0)
    throw std::invalid_argument("sample must contain non-empty mono or stereo interleaved audio");
  auto index=find_slot(id);
  if(index==std::numeric_limits<std::size_t>::max()){index=samples_.size();samples_.push_back({id,std::move(sample)});}
  else samples_[index].sample=std::move(sample);
  for(std::size_t note=0;note<note_slots_.size();++note)if(note_slots_[note]==id)note_sample_indices_[note]=index;
}

bool AudioEngine::map_note_to_slot(std::uint8_t note,SampleSlotId id) noexcept{
  if(processing_started_.load(std::memory_order_acquire))return false;
  const auto index=find_slot(id);if(index==std::numeric_limits<std::size_t>::max())return false;
  note_slots_[note]=id;note_sample_indices_[note]=index;return true;
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

bool AudioEngine::queue_timeline(const Timeline& timeline,std::string& error){
  reclaim_retired_plans();auto plan=std::make_unique<PlaybackPlan>();if(!compile_timeline(timeline,*plan,error))return false;
  if(!timelines_.push(plan.get())){dropped_timelines_.fetch_add(1,std::memory_order_relaxed);error="timeline queue is full";return false;}
  plan.release();
  error.clear();return true;
}

void AudioEngine::reclaim_retired_plans() noexcept{PlaybackPlan* plan{};while(retired_timelines_.pop(plan))delete plan;}
void AudioEngine::play() noexcept{if(!transport_commands_.push(TransportCommand::play))dropped_transport_commands_.fetch_add(1,std::memory_order_relaxed);}
void AudioEngine::stop() noexcept{if(!transport_commands_.push(TransportCommand::stop))dropped_transport_commands_.fetch_add(1,std::memory_order_relaxed);}
void AudioEngine::restart() noexcept{if(!transport_commands_.push(TransportCommand::restart))dropped_transport_commands_.fetch_add(1,std::memory_order_relaxed);}

void AudioEngine::start_voice(std::uint8_t note,float velocity,TrackId track,EventId event,bool timeline_owned) noexcept {
  const auto sample_index=note_sample_indices_[note];
  if(sample_index==std::numeric_limits<std::size_t>::max()){missing_slot_triggers_.fetch_add(1,std::memory_order_relaxed);return;}
  auto selected=voices_.size();
  for(std::size_t i=0;i<voices_.size();++i)if(!voices_[i].active){selected=i;break;}
  if(selected==voices_.size()){
    selected=0;
    for(std::size_t i=1;i<voices_.size();++i)
      if(voices_[i].age<voices_[selected].age)selected=i;
    stolen_voices_.fetch_add(1,std::memory_order_relaxed);
  }
  voices_[selected]={sample_index,0,velocity,next_voice_age_++,track,event,note,timeline_owned,true};
}

void AudioEngine::stop_timeline_voice(TrackId track,EventId event,std::uint8_t pitch) noexcept{
  auto selected=voices_.size();
  for(std::size_t i=0;i<voices_.size();++i)if(voices_[i].active&&voices_[i].timeline_owned&&voices_[i].track==track&&voices_[i].event==event&&
      (selected==voices_.size()||voices_[i].age<voices_[selected].age))selected=i;
  if(selected==voices_.size())for(std::size_t i=0;i<voices_.size();++i)if(voices_[i].active&&voices_[i].timeline_owned&&voices_[i].track==track&&voices_[i].pitch==pitch&&
      (selected==voices_.size()||voices_[i].age<voices_[selected].age))selected=i;
  if(selected<voices_.size())voices_[selected].active=false;
}

void AudioEngine::stop_voices(bool timeline_only) noexcept{for(auto& voice:voices_)if(!timeline_only||voice.timeline_owned)voice.active=false;}

void AudioEngine::process(std::span<float> left,std::span<float> right) noexcept {
  processing_started_.store(true,std::memory_order_release);
  const auto frames=std::min(left.size(),right.size());
  std::fill(left.begin(),left.end(),0);std::fill(right.begin(),right.end(),0);

  Pattern pattern;
  while(patterns_.pop(pattern))scheduler_.request_pattern(pattern);
  PlaybackPlan* timeline{};
  while(timelines_.pop(timeline)){
    stop_voices(true);
    if(active_timeline_!=nullptr&&!retired_timelines_.push(active_timeline_))dropped_timelines_.fetch_add(1,std::memory_order_relaxed);
    active_timeline_=timeline;timeline_scheduler_.set_plan(timeline);scheduler_.set_tempo(timeline->transport.microseconds_per_quarter);
  }
  TransportCommand command;
  while(transport_commands_.pop(command)){
    if(command==TransportCommand::play)transport_playing_=true;
    else if(command==TransportCommand::stop){transport_playing_=false;stop_voices();}
    else {transport_frame_=0;transport_playing_=true;stop_voices();}
  }

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

  const auto scheduled_count=transport_playing_?scheduler_.schedule(transport_frame_,static_cast<std::uint32_t>(frames),legacy_events_):0;
  const auto timeline_count=transport_playing_?timeline_scheduler_.schedule(transport_frame_,static_cast<std::uint32_t>(frames),timeline_events_):0;
  dropped_timeline_events_.store(timeline_scheduler_.dropped_events(),std::memory_order_release);
  dropped_legacy_events_.store(scheduler_.dropped_events(),std::memory_order_release);
  std::size_t external_index{},scheduled_index{},timeline_index{};

  for(std::size_t frame=0;frame<frames;++frame){
    while(external_index<pending_trigger_count_&&pending_triggers_[external_index].frame_offset==frame)
      {const auto& event=pending_triggers_[external_index++];start_voice(event.note,event.velocity);}
    while(scheduled_index<scheduled_count&&legacy_events_[scheduled_index].frame==frame)
      {const auto& event=legacy_events_[scheduled_index++];start_voice(event.note,event.velocity);}
    while(timeline_index<timeline_count&&timeline_events_[timeline_index].frame==frame){
      const auto& event=timeline_events_[timeline_index++];
      if(event.type==MusicalEventType::note_on)start_voice(event.pitch,event.value,event.track,event.id,true);
      else if(event.type==MusicalEventType::note_off)stop_timeline_voice(event.track,event.id,event.pitch);
    }
    for(auto& voice:voices_){
      if(!voice.active)continue;
      const auto& sample=samples_[voice.sample_index].sample;
      if(voice.cursor+sample.channels>sample.interleaved.size()){voice.active=false;continue;}
      left[frame]+=sample.interleaved[voice.cursor]*voice.gain;
      right[frame]+=sample.interleaved[voice.cursor+(sample.channels>1?1:0)]*voice.gain;
      voice.cursor+=sample.channels;
      if(voice.cursor>=sample.interleaved.size())voice.active=false;
    }
  }

  const auto remaining=pending_trigger_count_-external_index;
  for(std::size_t i=0;i<remaining;++i){
    pending_triggers_[i]=pending_triggers_[external_index+i];
    pending_triggers_[i].frame_offset-=static_cast<std::uint32_t>(frames);
  }
  pending_trigger_count_=remaining;
  if(transport_playing_){
    if(frames>std::numeric_limits<std::uint64_t>::max()-transport_frame_){transport_frame_=std::numeric_limits<std::uint64_t>::max();transport_playing_=false;stop_voices();}
    else transport_frame_+=frames;
  }
  published_frame_.store(transport_frame_,std::memory_order_release);published_playing_.store(transport_playing_,std::memory_order_release);
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
