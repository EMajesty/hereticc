#include "neurogenic/audio/engine.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const char* message){if(std::fabs(actual-expected)>0.0001F)throw std::runtime_error(message);}

neurogenic::Sample constant_sample(std::size_t frames,float value=1.0F){
  return {48000,1,std::vector<float>(frames,value)};
}

void test_queue_and_scheduler(){
  using namespace neurogenic;
  SpscQueue<int,4> queue;check(queue.push(1)&&queue.push(2)&&queue.push(3),"queue fill");
  check(!queue.push(4),"queue overflow");int item{};check(queue.pop(item)&&item==1,"queue order");

  Scheduler scheduler(1600,60);Pattern first;first.steps[0]={true,42,1};first.steps[4]={true,42,1};first.revision=1;
  scheduler.request_pattern(first);std::array<ScheduledEvent,64> events{};auto count=scheduler.schedule(1601,events);
  check(count==2&&events[0].frame==0&&events[1].frame==1600,"sample offsets");
  Pattern second;second.steps[4]={true,43,1};second.revision=2;scheduler.request_pattern(second);
  scheduler.schedule(4799,events);check(scheduler.active_pattern().revision==1,"early pattern swap");
  scheduler.schedule(2,events);check(scheduler.active_pattern().revision==2,"bar pattern swap");

  Scheduler drift(44100,123.0);Pattern every;for(auto& step:every.steps)step.enabled=true;drift.request_pattern(every);
  std::uint64_t absolute{},seen{};
  for(std::size_t call=0;seen<5000;++call){
    const auto frames=static_cast<std::uint32_t>((call*7919)%997+1);auto n=drift.schedule(frames,events);
    for(std::size_t i=0;i<n;++i){
      const auto expected=static_cast<std::uint64_t>(std::llround(static_cast<long double>(seen)*44100.0L*60.0L/123.0L/4.0L));
      check(absolute+events[i].frame==expected,"scheduler drift");++seen;
    }absolute+=frames;
  }
}

void test_polyphony_and_events(){
  using namespace neurogenic;
  AudioEngine overlap(48000,4);overlap.set_sample(constant_sample(8));
  overlap.trigger(60,1,0);overlap.trigger(61,1,1);
  std::array<float,8> left{},right{};overlap.process(left,right);
  near(left[0],0.8F,"first voice start");near(left[1],1.6F,"overlapping retrigger");

  AudioEngine simultaneous(48000,8);simultaneous.set_sample(constant_sample(4));
  simultaneous.trigger(60,0.25F);simultaneous.trigger(61,0.5F);simultaneous.trigger(62,1.0F);
  simultaneous.process(left,right);near(left[0],1.4F,"simultaneous triggers were not all consumed");

  AudioEngine ordered(48000,2);ordered.set_sample(constant_sample(4));
  ordered.trigger(60,0.25F);ordered.trigger(61,0.5F);ordered.trigger(62,1.0F);
  ordered.process(left,right);near(left[0],1.2F,"equal-offset enqueue ordering");

  AudioEngine offsets(48000,4);offsets.set_sample(constant_sample(8));offsets.trigger(60,1,5);
  offsets.process({left.data(),2},{right.data(),2});near(left[0],0,"early frame offset");
  offsets.process({left.data(),4},{right.data(),4});near(left[2],0,"frame before carried event");near(left[3],0.8F,"carried frame offset");

  AudioEngine stealing(48000,2);stealing.set_sample(constant_sample(8));
  stealing.trigger(60,0.25F,0);stealing.trigger(61,0.5F,1);stealing.trigger(62,1.0F,2);
  stealing.process(left,right);near(left[2],1.2F,"oldest voice stealing policy");check(stealing.stolen_voices()==1,"voice stealing counter");

  AudioEngine overflow;overflow.set_sample(constant_sample(2));
  for(int i=0;i<100;++i)overflow.trigger(60,1);
  check(overflow.dropped_triggers()==36,"trigger overflow count");overflow.process(left,right);
  check(overflow.stolen_voices()==32,"burst voice allocation");near(left[0],25.6F,"burst trigger delivery");

  AudioEngine variable(48000,4);variable.set_sample(constant_sample(32));variable.trigger(60,1,12);
  std::size_t elapsed{};for(auto size:{1U,3U,5U,7U}){variable.process({left.data(),size},{right.data(),size});for(std::size_t i=0;i<size;++i)if(elapsed+i==12)near(left[i],0.8F,"variable callback event offset");elapsed+=size;}

  Pattern invalid;invalid.steps[0].velocity=2;check(!overflow.queue_pattern(invalid),"invalid pattern accepted");
  bool rejected=false;try{overflow.set_sample(constant_sample(1));}catch(const std::logic_error&){rejected=true;}
  check(rejected,"sample replacement raced processing");
}

void test_sample_slots(){
  using namespace neurogenic;
  std::array<float,8> left{},right{};
  AudioEngine engine(48000,8);
  engine.set_sample_slot(SampleSlotId{0},constant_sample(8,0.25F));
  engine.set_sample_slot(SampleSlotId{1},constant_sample(8,0.5F));
  check(engine.sample_slot_count()==2&&engine.has_sample_slot(SampleSlotId{1}),"sample slot installation");
  engine.trigger(36,1);engine.trigger(37,1);engine.process(left,right);
  near(left[0],0.6F,"different notes did not select different samples");

  AudioEngine same(48000,4);same.set_sample_slot(SampleSlotId{0},constant_sample(4,0.25F));
  same.trigger(36,1);same.trigger(36,1);same.process(left,right);near(left[0],0.4F,"same-slot polyphony");

  AudioEngine missing;missing.set_sample_slot(SampleSlotId{0},constant_sample(4));missing.trigger(37,1);missing.trigger(52,1);
  missing.process(left,right);near(left[0],0,"missing slots produced audio");check(missing.missing_slot_triggers()==2,"missing slot counter");
  check(!missing.map_note_to_slot(52,SampleSlotId{999}),"invalid slot mapping accepted");

  AudioEngine arbitrary;arbitrary.set_sample_slot(SampleSlotId{1000},constant_sample(4,0.75F));
  check(arbitrary.map_note_to_slot(52,SampleSlotId{1000}),"arbitrary slot mapping rejected");
  arbitrary.trigger(52,1);arbitrary.process(left,right);near(left[0],0.6F,"arbitrary slot did not play");

  AudioEngine stealing(48000,2);stealing.set_sample_slot(SampleSlotId{0},constant_sample(8,0.25F));
  stealing.set_sample_slot(SampleSlotId{1},constant_sample(8,0.5F));stealing.set_sample_slot(SampleSlotId{2},constant_sample(8,1.0F));
  stealing.trigger(36,1);stealing.trigger(37,1);stealing.trigger(38,1);stealing.process(left,right);
  near(left[0],1.2F,"voice stealing across samples");check(stealing.stolen_voices()==1,"cross-sample steal counter");

  AudioEngine sequenced;sequenced.set_sample_slot(SampleSlotId{1},constant_sample(8,0.5F));
  Pattern pattern;pattern.steps[0]={true,37,1};pattern.revision=1;check(sequenced.queue_pattern(pattern),"sequencer slot pattern queue");
  sequenced.process(left,right);near(left[0],0.4F,"sequencer note did not select slot");

  AudioEngine midi_engine; midi_engine.set_sample_slot(SampleSlotId{1},constant_sample(8,0.5F));MidiMapper mapper;
  mapper.set_note_handler([&](auto note,auto velocity){midi_engine.trigger(note,velocity);});
  check(mapper.receive({0x90,37,127,0}),"slot MIDI enqueue");check(mapper.dispatch_pending()==1,"slot MIDI dispatch");
  midi_engine.process(left,right);near(left[0],0.4F,"MIDI note did not select slot");

  bool invalid_sample=false;try{AudioEngine invalid;invalid.set_sample_slot(SampleSlotId{0},{44100,1,{1.0F}});}catch(const std::invalid_argument&){invalid_sample=true;}
  check(invalid_sample,"mismatched sample rate accepted");
  bool invalid_id=false;try{AudioEngine invalid;invalid.set_sample_slot(invalid_sample_slot,constant_sample(1));}catch(const std::invalid_argument&){invalid_id=true;}
  check(invalid_id,"reserved sample slot identifier accepted");
  check(!AudioEngine{}.map_note_to_slot(36,SampleSlotId{0}),"unloaded default slot mapped");

  const auto invalid_path=std::filesystem::temp_directory_path()/"neurogenic-invalid.wav";
  {std::ofstream invalid(invalid_path,std::ios::binary);invalid<<"not a wave";}
  bool invalid_wav=false;try{(void)load_wav(invalid_path);}catch(const std::runtime_error&){invalid_wav=true;}
  std::filesystem::remove(invalid_path);check(invalid_wav,"invalid WAV accepted");
  bool missing_wav=false;try{(void)load_wav(invalid_path);}catch(const std::runtime_error&){missing_wav=true;}
  check(missing_wav,"missing WAV accepted");
}

void test_midi_control_dispatch(){
  using namespace neurogenic;
  MidiMapper mapper;std::atomic<int> notes{},actions{};std::thread::id action_thread;
  mapper.set_note_handler([&](auto,float){++notes;});
  mapper.map_button(0,12,"test",[&]{action_thread=std::this_thread::get_id();++actions;});
  check(mapper.receive({0x90,12,1,0}),"MIDI enqueue");check(actions==0,"action ran in MIDI callback");
  check(mapper.dispatch_pending()==1&&notes==1&&actions==1,"deterministic MIDI dispatch");
  check(!mapper.receive({0x80,12,1,0}),"MIDI note-off filtering");

  MidiMapper threaded;std::promise<void> delivered;auto ready=delivered.get_future();std::thread::id worker_id;
  threaded.map_button(0,13,"threaded",[&]{worker_id=std::this_thread::get_id();delivered.set_value();});
  ControlDispatcher dispatcher(threaded);dispatcher.start();check(threaded.receive({0x90,13,127,0}),"threaded MIDI enqueue");
  ready.get();dispatcher.stop();check(worker_id!=std::this_thread::get_id(),"action did not run on control thread");

  MidiMapper pending;std::atomic<int> drained{};pending.map_button(0,14,"drain",[&]{++drained;});
  for(int i=0;i<20;++i)check(pending.receive({0x90,14,1,0}),"pending MIDI enqueue");
  ControlDispatcher shutdown(pending);shutdown.start();shutdown.stop();check(drained==20,"shutdown did not drain pending events");

  MidiMapper full;for(int i=0;i<300;++i)full.receive({0x90,15,1,0});
  check(full.dropped_messages()==44,"MIDI queue overflow count");check(full.dispatch_pending()==256,"MIDI queue capacity");
}

void test_lua_and_offline(){
  using namespace neurogenic;
  bool submitted=false;ScriptRuntime lua([&](Pattern pattern){submitted=pattern.steps[0].enabled&&pattern.steps[0].note==64;return true;},[]{return 123ULL;});std::string error;
#ifdef NEUROGENIC_HAS_LUA
  check(lua.execute("local p=neurogenic.v1.pattern.new();p:set_step(1,true,64,0.75);neurogenic.v1.pattern.queue(p)",error)&&submitted,"Lua execution");
  check(!lua.execute("this is not lua",error)&&!error.empty(),"invalid Lua accepted");
  check(!lua.execute("local p=neurogenic.v1.pattern.new();p:set_step(1,true,200,1)",error),"invalid Lua pattern accepted");
  const auto script_path=std::filesystem::temp_directory_path()/"neurogenic-script-test.lua";
  {std::ofstream script(script_path);script<<"local p=neurogenic.v1.pattern.new(); p:set_step(1,true,64,1); neurogenic.v1.pattern.queue(p)\n";}
  submitted=false;check(lua.execute_file(script_path,error)&&submitted,"Lua file execution");std::filesystem::remove(script_path);
#else
  check(!lua.execute("return 1",error)&&!error.empty(),"Lua unexpectedly available");
#endif
  const auto path=std::filesystem::temp_directory_path()/"neurogenic-offline-test.wav";
  AudioEngine offline;offline.set_sample(constant_sample(16));offline.trigger(60,1);offline.render_wav(path,64);
  std::ifstream file(path,std::ios::binary);std::array<char,4> riff{};file.read(riff.data(),4);
  check(std::string(riff.data(),4)=="RIFF","offline WAV");std::filesystem::remove(path);
}
}

int main(){
  try{test_queue_and_scheduler();test_polyphony_and_events();test_sample_slots();test_midi_control_dispatch();test_lua_and_offline();std::cout<<"all tests passed\n";return 0;}
  catch(const std::exception& error){std::cerr<<"TEST FAILURE: "<<error.what()<<'\n';return 1;}
}
