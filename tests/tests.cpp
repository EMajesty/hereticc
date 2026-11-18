#include "neurogenic/audio/engine.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/sequencer/pattern_adapter.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/timeline/io.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
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
  scheduler.request_pattern(first);std::array<ScheduledEvent,64> events{};auto count=scheduler.schedule(0,1601,events);
  check(count==2&&events[0].frame==0&&events[1].frame==1600,"sample offsets");
  Pattern second;second.steps[4]={true,43,1};second.revision=2;scheduler.request_pattern(second);
  scheduler.schedule(1601,4799,events);check(scheduler.active_pattern().revision==1,"early pattern swap");
  scheduler.schedule(6400,2,events);check(scheduler.active_pattern().revision==2,"bar pattern swap");

  Scheduler drift(44100,123.0);Pattern every;for(auto& step:every.steps)step.enabled=true;drift.request_pattern(every);
  std::uint64_t absolute{},seen{};
  for(std::size_t call=0;seen<5000;++call){
    const auto frames=static_cast<std::uint32_t>((call*7919)%997+1);auto n=drift.schedule(absolute,frames,events);
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

void test_timeline_and_tracks(){
  using namespace neurogenic;
  Timeline timeline;timeline.transport={500000,7,8};
  Track second;second.id=20;second.pattern.length=720;
  second.pattern.events.push_back({3,0,MusicalEventType::note,37,0.5F,240,0,0});
  Track first;first.id=10;first.pattern.length=960;
  first.pattern.events.push_back({2,0,MusicalEventType::note_on,36,1,0,0,0});
  first.pattern.events.push_back({1,0,MusicalEventType::note_on,38,0.25F,0,0,0});
  first.pattern.events.push_back({4,480,MusicalEventType::note_off,36,0,0,0,0});
  first.pattern.events.push_back({5,240,MusicalEventType::parameter,0,0,0,77,0.75F});
  timeline.tracks={second,first};

  PlaybackPlan plan;std::string error;check(compile_timeline(timeline,plan,error),"timeline compilation");
  check(plan.tracks.size()==2&&plan.events.size()==6,"compiled timeline dimensions");
  TimelineScheduler scheduler(48000);scheduler.set_plan(&plan);
  check(scheduler.frame_at_tick(960)==24000,"tempo-to-frame conversion");
  std::array<TimelineScheduledEvent,32> events{};auto count=scheduler.schedule(0,1,events);
  check(count==3,"simultaneous multi-track events");
  check(events[0].track==10&&events[0].id==1&&events[1].id==2&&events[2].track==20,"deterministic event ordering");

  Timeline variable;Track odd;odd.id=1;odd.pattern.length=721;
  odd.pattern.events.push_back({1,0,MusicalEventType::note_on,36,1,0,0,0});variable.tracks.push_back(odd);
  check(compile_timeline(variable,plan,error),"arbitrary pattern length compilation");scheduler=TimelineScheduler(44100);scheduler.set_plan(&plan);
  std::uint64_t absolute{};std::vector<std::uint64_t> observed;
  for(std::size_t call=0;observed.size()<20;++call){
    const auto frames=static_cast<std::uint32_t>((call*37)%509+1);count=scheduler.schedule(absolute,frames,events);
    for(std::size_t i=0;i<count;++i)observed.push_back(absolute+events[i].frame);
    absolute+=frames;
  }
  for(std::size_t i=0;i<observed.size();++i){
    const auto expected=static_cast<std::uint64_t>(std::llround(static_cast<long double>(i)*721*44100*500000/(960.0L*1000000)));
    check(observed[i]==expected,"timeline timing drift across variable buffers");
  }

  Timeline muted=variable;muted.tracks[0].muted=true;check(compile_timeline(muted,plan,error),"muted timeline compilation");
  scheduler=TimelineScheduler(48000);scheduler.set_plan(&plan);check(scheduler.schedule(0,48000,events)==0,"muted track produced events");

  Timeline boundary;Track looping;looping.id=9;looping.pattern.length=960;
  looping.pattern.events={{1,959,MusicalEventType::note_on,36,1,0,0,0},{2,0,MusicalEventType::note_on,37,1,0,0,0}};
  boundary.tracks.push_back(looping);check(compile_timeline(boundary,plan,error),"loop boundary compilation");
  scheduler=TimelineScheduler(48000);scheduler.set_plan(&plan);count=scheduler.schedule(0,24001,events);
  check(count==3&&events[0].pitch==37&&events[1].pitch==36&&events[2].pitch==37&&events[2].frame==24000,"loop boundary scheduling");

  Timeline invalid=boundary;invalid.tracks[0].pattern.events[1].id=1;
  check(!compile_timeline(invalid,plan,error)&&!error.empty(),"duplicate event identifier accepted");
  Timeline scalable;Track crowded;crowded.id=1;crowded.pattern.length=2000;
  for(std::size_t i=0;i<1000;++i)crowded.pattern.events.push_back({i+1,static_cast<MusicalTick>(i),MusicalEventType::note_on,36,1,0,0,0});
  scalable.tracks.push_back(std::move(crowded));check(compile_timeline(scalable,plan,error)&&plan.events.size()==1000,"preparation-time timeline scaling");

  AudioEngine plan_queue;Timeline one;Track one_track;one_track.id=1;one_track.pattern.length=960;
  one_track.pattern.events.push_back({1,0,MusicalEventType::note_on,36,1,0,0,0});one.tracks.push_back(one_track);
  check(plan_queue.queue_timeline(one,error)&&plan_queue.queue_timeline(one,error)&&plan_queue.queue_timeline(one,error),"timeline queue usable capacity");
  check(!plan_queue.queue_timeline(one,error)&&plan_queue.dropped_timelines()==1,"timeline queue overflow behavior");

  AudioEngine engine(48000,8);engine.set_sample_slot(SampleSlotId{0},constant_sample(30000,0.25F));
  engine.set_sample_slot(SampleSlotId{1},constant_sample(30000,0.5F));
  Timeline audio;Track audio_track;audio_track.id=1;audio_track.pattern.length=960;
  audio_track.pattern.events={{1,0,MusicalEventType::note,36,1,480,0,0},{2,0,MusicalEventType::note,37,1,480,0,0}};
  audio.tracks.push_back(audio_track);check(engine.queue_timeline(audio,error),"audio timeline queue");
  std::vector<float> left(12000),right(12000);engine.process(left,right);
  near(left[0],0.6F,"timeline tracks did not select sample slots");near(left.back(),0.6F,"note duration ended early");
  std::array<float,2> tail_left{},tail_right{};engine.process(tail_left,tail_right);near(tail_left[0],0,"note-off did not stop timeline voices");

  AudioEngine durations(48000,4);durations.set_sample_slot(SampleSlotId{0},constant_sample(30000));
  Timeline overlap;Track overlap_track;overlap_track.id=2;overlap_track.pattern.length=1920;
  overlap_track.pattern.events={{1,0,MusicalEventType::note,36,0.25F,480,0,0},{2,0,MusicalEventType::note,36,0.5F,720,0,0}};
  overlap.tracks.push_back(overlap_track);check(durations.queue_timeline(overlap,error),"overlapping duration queue");
  durations.process(left,right);near(left[0],0.6F,"overlapping duration notes did not start");
  durations.process(tail_left,tail_right);near(tail_left[0],0.4F,"one note-off stopped an unrelated overlapping note");
  std::vector<float> remainder_left(5998),remainder_right(5998);durations.process(remainder_left,remainder_right);
  durations.process(tail_left,tail_right);near(tail_left[0],0,"second duration note did not stop");

  Pattern legacy;legacy.steps[3]={true,39,0.75F};legacy.revision=4;
  auto adapted=adapt_step_pattern(legacy,42);check(adapted.tracks.size()==1&&adapted.tracks[0].id==42,"legacy pattern track adapter");
  check(adapted.tracks[0].pattern.length==ticks_per_quarter*4&&adapted.tracks[0].pattern.events[0].tick==ticks_per_quarter*3/4,"legacy pattern timing adapter");
  check(compile_timeline(adapted,plan,error),"legacy pattern did not compile");
}

void test_transport_integration_and_timeline_io(){
  using namespace neurogenic;std::string error;
  Timeline timeline;Track track;track.id=1;track.pattern.length=3840;
  track.pattern.events.push_back({1,0,MusicalEventType::note,36,1,1920,0,0});timeline.tracks.push_back(track);
  AudioEngine transport(48000,4);transport.set_sample_slot(SampleSlotId{0},constant_sample(100000));
  check(transport.queue_timeline(timeline,error),"transport timeline queue");std::array<float,4> left{},right{};
  transport.process({left.data(),1},{right.data(),1});near(left[0],0.8F,"transport did not initially play");check(transport.frame_position()==1&&transport.playing(),"initial transport state");
  transport.stop();transport.process({left.data(),2},{right.data(),2});near(left[0],0,"stop did not clean voices");check(transport.frame_position()==1&&!transport.playing(),"stopped transport advanced");
  transport.play();transport.process({left.data(),1},{right.data(),1});near(left[0],0,"play unexpectedly restarted timeline");check(transport.frame_position()==2&&transport.playing(),"play did not resume");
  transport.restart();transport.process({left.data(),1},{right.data(),1});near(left[0],0.8F,"restart did not return to tick zero");check(transport.frame_position()==1,"restart frame position");
  AudioEngine command_overflow;for(int i=0;i<20;++i)command_overflow.play();check(command_overflow.dropped_transport_commands()==5,"transport command overflow counter");

  Timeline replacement;Track muted;muted.id=2;muted.muted=true;muted.pattern.length=960;replacement.tracks.push_back(muted);
  check(transport.queue_timeline(replacement,error),"replacement queue");transport.process({left.data(),1},{right.data(),1});near(left[0],0,"timeline replacement did not clean active notes");

  Timeline crossing;Track short_loop;short_loop.id=8;short_loop.pattern.length=480;
  short_loop.pattern.events.push_back({99,0,MusicalEventType::note,36,1,720,0,0});crossing.tracks.push_back(short_loop);
  PlaybackPlan crossing_plan;check(compile_timeline(crossing,crossing_plan,error),"cross-loop duration compilation");TimelineScheduler crossing_scheduler(48000);crossing_scheduler.set_plan(&crossing_plan);
  std::array<TimelineScheduledEvent,16> scheduled{};const auto crossing_count=crossing_scheduler.schedule(0,30001,scheduled);
  check(crossing_count==5&&scheduled[0].type==MusicalEventType::note_on&&scheduled[1].frame==12000&&scheduled[2].type==MusicalEventType::note_off&&scheduled[2].frame==18000,"cross-loop note scheduling");
  Timeline tiny;Track tiny_track;tiny_track.id=1;tiny_track.pattern.length=1;tiny_track.pattern.events.push_back({1,0,MusicalEventType::note_on,36,1,0,0,0});tiny.tracks.push_back(tiny_track);
  PlaybackPlan tiny_plan;check(compile_timeline(tiny,tiny_plan,error),"one-tick loop compilation");TimelineScheduler tiny_scheduler(48000);tiny_scheduler.set_plan(&tiny_plan);check(tiny_scheduler.schedule(0,100,scheduled)==4,"one-tick loop scheduling");

  AudioEngine edges(48000,2);edges.set_sample_slot(SampleSlotId{0},constant_sample(100));Timeline edge_timeline;Track edge_track;edge_track.id=5;edge_track.pattern.length=960;
  edge_track.pattern.events={{1,0,MusicalEventType::note_on,36,1,0,0,0},{2,1,MusicalEventType::note_off,36,0,0,0,0}};edge_timeline.tracks.push_back(edge_track);
  check(edges.queue_timeline(edge_timeline,error),"explicit note edges queue");std::array<float,26> edge_left{},edge_right{};edges.process(edge_left,edge_right);near(edge_left[0],0.8F,"explicit note-on missing");near(edge_left[25],0,"explicit note-off did not match pitch");

  AudioEngine overflow(48000,4,2);overflow.set_sample_slot(SampleSlotId{0},constant_sample(2));Timeline burst;Track burst_track;burst_track.id=1;burst_track.pattern.length=960;
  for(std::uint64_t id=1;id<=5;++id)burst_track.pattern.events.push_back({id,0,MusicalEventType::note_on,36,1,0,0,0});burst.tracks.push_back(burst_track);
  check(overflow.queue_timeline(burst,error),"staging overflow timeline queue");overflow.process(left,right);check(overflow.dropped_timeline_events()==3,"timeline staging overflow counter");

  AudioEngine shared(48000,8);shared.set_sample_slot(SampleSlotId{0},constant_sample(2,0.25F));shared.set_sample_slot(SampleSlotId{1},constant_sample(2,0.5F));
  Timeline slow;slow.transport.microseconds_per_quarter=1'000'000;Track slow_track;slow_track.id=1;slow_track.pattern.length=3840;slow_track.pattern.events.push_back({1,960,MusicalEventType::note_on,37,1,0,0,0});slow.tracks.push_back(slow_track);
  Pattern legacy;legacy.steps[4]={true,36,1};legacy.revision=1;check(shared.queue_pattern(legacy)&&shared.queue_timeline(slow,error),"shared transport setup");
  std::vector<float> before_left(48000),before_right(48000);shared.process(before_left,before_right);near(before_left.back(),0,"shared transport events arrived early");
  shared.process({left.data(),1},{right.data(),1});near(left[0],0.6F,"legacy and timeline workflows do not share tempo/position");

  AudioEngine tempo_change(48000,2);tempo_change.set_sample_slot(SampleSlotId{0},constant_sample(8));Timeline empty;Track empty_track;empty_track.id=1;empty_track.pattern.length=960;empty.tracks.push_back(empty_track);check(tempo_change.queue_timeline(empty,error),"initial tempo plan");
  std::vector<float> advance_left(12000),advance_right(12000);tempo_change.process(advance_left,advance_right);Timeline changed;changed.transport.microseconds_per_quarter=1'000'000;Track changed_track;changed_track.id=1;changed_track.pattern.length=960;changed_track.pattern.events.push_back({1,240,MusicalEventType::note_on,36,1,0,0,0});changed.tracks.push_back(changed_track);
  check(tempo_change.queue_timeline(changed,error),"tempo transition plan");tempo_change.process({left.data(),1},{right.data(),1});near(left[0],0.8F,"tempo transition did not use authoritative position");

  Timeline large;Track large_track;large_track.id=1;large_track.pattern.length=std::numeric_limits<MusicalTick>::max();large_track.pattern.events.push_back({1,std::numeric_limits<MusicalTick>::max()-1,MusicalEventType::note_on,36,1,0,0,0});large.tracks.push_back(large_track);
  PlaybackPlan large_plan;check(compile_timeline(large,large_plan,error),"large-position compilation");TimelineScheduler large_scheduler(192000);large_scheduler.set_plan(&large_plan);check(large_scheduler.frame_at_tick(std::numeric_limits<MusicalTick>::max()-1)==std::numeric_limits<std::uint64_t>::max(),"large position did not saturate safely");

  const auto path=std::filesystem::temp_directory_path()/"neurogenic-timeline-test.ngt";save_timeline(timeline,path);auto loaded=load_timeline(path);std::filesystem::remove(path);
  check(loaded.tracks.size()==1&&loaded.tracks[0].pattern.events[0].duration==1920&&loaded.tracks[0].id==1,"timeline file round trip");
  const auto invalid_path=std::filesystem::temp_directory_path()/"neurogenic-invalid-timeline.ngt";{std::ofstream invalid(invalid_path);invalid<<"NEUROGENIC_TIMELINE 99\n";}
  bool rejected=false;try{(void)load_timeline(invalid_path);}catch(const std::runtime_error&){rejected=true;}std::filesystem::remove(invalid_path);check(rejected,"unsupported timeline version accepted");
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
  try{test_queue_and_scheduler();test_polyphony_and_events();test_sample_slots();test_timeline_and_tracks();test_transport_integration_and_timeline_io();test_midi_control_dispatch();test_lua_and_offline();std::cout<<"all tests passed\n";return 0;}
  catch(const std::exception& error){std::cerr<<"TEST FAILURE: "<<error.what()<<'\n';return 1;}
}
