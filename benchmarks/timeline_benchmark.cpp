#include "neurogenic/audio/engine.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
volatile std::uint64_t checksum{};

neurogenic::Timeline make_timeline(std::size_t tracks,std::size_t events,bool dense=false,bool far=false){
  neurogenic::Timeline timeline;
  for(std::size_t ti=0;ti<tracks;++ti){
    neurogenic::Track track;track.id=ti+1;track.pattern.length=dense?1:960*2048;
    for(std::size_t ei=0;ei<events;++ei)track.pattern.events.push_back({ei+1,dense?0:static_cast<neurogenic::MusicalTick>((far?500000:0)+ei*8),neurogenic::MusicalEventType::note_on,static_cast<std::uint8_t>(36+ti%16),0.5F,0,0,0});
    timeline.tracks.push_back(std::move(track));
  }
  return timeline;
}

void scheduler_case(const char* name,std::size_t tracks,std::size_t events,std::uint32_t frames,bool dense=false,bool far=false){
  auto timeline=make_timeline(tracks,events,dense,far);neurogenic::PlaybackPlan plan;std::string error;
  if(!neurogenic::compile_timeline(timeline,plan,error))throw std::runtime_error(error);
  neurogenic::TimelineScheduler scheduler(48000);scheduler.set_plan(&plan,0);std::vector<neurogenic::TimelineScheduledEvent> output(512);
  constexpr std::size_t iterations=4000;std::uint64_t position{};const auto begin=Clock::now();
  for(std::size_t i=0;i<iterations;++i){checksum+=scheduler.schedule(position,frames,output);position+=frames;}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();
  std::cout<<name<<": "<<elapsed/iterations<<" ns/callback (tracks="<<tracks<<", events/track="<<events<<", frames="<<frames<<")\n";
}

void polyphony_case(){
  neurogenic::AudioEngine engine(48000,64);engine.set_sample({48000,1,std::vector<float>(4096,0.25F)});std::vector<float> left(256),right(256);
  constexpr std::size_t iterations=4000;const auto begin=Clock::now();
  for(std::size_t i=0;i<iterations;++i){for(std::uint8_t note=36;note<68;++note)engine.trigger(note,0.5F);engine.process(left,right);checksum+=left[0]!=0;}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();std::cout<<"polyphony_32: "<<elapsed/iterations<<" ns/callback (256 frames)\n";
}

void replacement_case(){
  neurogenic::AudioEngine engine;auto timeline=make_timeline(8,64);std::vector<float> left(64),right(64);std::string error;
  constexpr std::size_t iterations=1000;const auto begin=Clock::now();
  for(std::size_t i=0;i<iterations;++i){if(!engine.queue_timeline(timeline,error))throw std::runtime_error(error);engine.process(left,right);}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();std::cout<<"rapid_replacement: "<<elapsed/iterations<<" ns/compile+queue+callback\n";
}
}

int main(){
  scheduler_case("far_64",1,64,256,false,true);scheduler_case("far_65536",1,65536,256,false,true);
  scheduler_case("tracks_1",1,64,256);scheduler_case("tracks_16",16,64,256);scheduler_case("tracks_64",64,64,256);
  scheduler_case("callback_64",16,128,64);scheduler_case("callback_1024",16,128,1024);
  scheduler_case("dense_1024",1,1024,256,true);polyphony_case();replacement_case();std::cout<<"checksum="<<checksum<<'\n';
}
