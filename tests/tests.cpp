#include "neurogenic/audio/engine.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);} }
int main(){try{using namespace neurogenic;
  SpscQueue<int,4> queue;check(queue.push(1)&&queue.push(2)&&queue.push(3),"queue fill");check(!queue.push(4),"queue overflow");int q{};check(queue.pop(q)&&q==1,"queue order");
  Scheduler scheduler(1600,60);Pattern first;first.steps[0]={true,42,1};first.steps[4]={true,42,1};first.revision=1;scheduler.request_pattern(first);std::array<ScheduledEvent,64> events{};auto count=scheduler.schedule(1601,events);check(count==2&&events[0].frame==0&&events[1].frame==1600,"sample offsets");Pattern second;second.steps[4]={true,43,1};second.revision=2;scheduler.request_pattern(second);scheduler.schedule(4799,events);check(scheduler.active_pattern().revision==1,"early pattern swap");scheduler.schedule(2,events);check(scheduler.active_pattern().revision==2,"bar pattern swap");
  Scheduler drift(44100,123.0);Pattern every;for(auto& step:every.steps)step.enabled=true;drift.request_pattern(every);std::uint64_t absolute{};std::uint64_t seen{};for(std::size_t call=0;seen<5000;++call){const std::uint32_t frames=static_cast<std::uint32_t>((call*7919)%997+1);auto n=drift.schedule(frames,events);for(std::size_t i=0;i<n;++i){const auto expected=static_cast<std::uint64_t>(std::llround(static_cast<long double>(seen)*44100.0L*60.0L/123.0L/4.0L));check(absolute+events[i].frame==expected,"scheduler drift");++seen;}absolute+=frames;}
  bool mapped=false;MidiMapper mapper;mapper.map_button(0,12,"test",[&]{mapped=true;});check(mapper.receive({0x90,12,1,0})&&mapped,"MIDI dispatch");check(!mapper.receive({0x80,12,1,0}),"MIDI note-off filtering");
  AudioEngine engine;Sample sample{48000,1,std::vector<float>(16,1)};engine.set_sample(sample);for(int i=0;i<100;++i)engine.trigger(60,1);check(engine.dropped_triggers()==36,"trigger overflow count");std::array<float,37> left{},right{};for(auto size:{1U,7U,13U,37U})engine.process({left.data(),size},{right.data(),size});check(left[0]>0,"callback playback");Pattern invalid;invalid.steps[0].velocity=2;check(!engine.queue_pattern(invalid),"invalid pattern accepted");
  bool submitted=false;ScriptRuntime lua([&](Pattern pattern){submitted=pattern.steps[0].enabled&&pattern.steps[0].note==64;return true;},[]{return 123ULL;});std::string error;
#ifdef NEUROGENIC_HAS_LUA
  check(lua.execute("local p=neurogenic.v1.pattern.new();p:set_step(1,true,64,0.75);neurogenic.v1.pattern.queue(p)",error)&&submitted,"Lua execution");check(!lua.execute("this is not lua",error)&&!error.empty(),"invalid Lua accepted");check(!lua.execute("local p=neurogenic.v1.pattern.new();p:set_step(1,true,200,1)",error),"invalid Lua pattern accepted");const auto script_path=std::filesystem::temp_directory_path()/"neurogenic-script-test.lua";{std::ofstream script(script_path);script<<"local p=neurogenic.v1.pattern.new(); p:set_step(1,true,64,1); neurogenic.v1.pattern.queue(p)\n";}submitted=false;check(lua.execute_file(script_path,error)&&submitted,"Lua file execution");std::filesystem::remove(script_path);
#else
  check(!lua.execute("return 1",error)&&!error.empty(),"Lua unexpectedly available");
#endif
  const auto path=std::filesystem::temp_directory_path()/"neurogenic-offline-test.wav";AudioEngine offline;offline.set_sample(sample);offline.trigger(60,1);offline.render_wav(path,64);std::ifstream file(path,std::ios::binary);std::array<char,4> riff{};file.read(riff.data(),4);check(std::string(riff.data(),4)=="RIFF","offline WAV");std::filesystem::remove(path);
  std::cout<<"all tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<"TEST FAILURE: "<<error.what()<<'\n';return 1;}}
