#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/timeline/io.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> running{true};
void stop(int){running=false;}
void usage(){
  std::cout<<"usage: neurogenic [OPTIONS] [SAMPLE.wav]\n"
             "  --slot ID=FILE    load FILE into stable numeric sample slot ID (repeatable)\n"
             "  --map NOTE=ID     map MIDI/sequencer NOTE to a loaded slot (repeatable)\n"
             "  --audio NAME      select audio output by index or name\n"
             "  --midi NAME       select MIDI input by index or name\n"
             "  --script FILE     Lua action for MIDI note 36\n"
             "  --timeline FILE   load a NEUROGENIC_TIMELINE version 1 file\n"
             "  --interactive     accept live load/unload/status/quit commands on stdin\n"
             "  --list-devices    enumerate audio and MIDI devices\n"
             "  --headless        explicitly select null audio\n";
}

void print_status(const neurogenic::SampleSlotStatus& state){
  std::cout<<"slot "<<state.id.value<<" "<<neurogenic::sample_slot_state_name(state.state);
  if(state.has_sample)std::cout<<" source=\""<<state.source_path.string()<<"\" "<<state.source_sample_rate<<"->"<<state.engine_sample_rate<<" Hz "<<state.channels<<"ch "<<state.duration_seconds<<"s";
  if(!state.pending_source_path.empty())std::cout<<" pending=\""<<state.pending_source_path.string()<<'"';
  if(!state.error.empty())std::cout<<" error=\""<<state.error<<'"';std::cout<<'\n';
}

std::pair<std::uint32_t,std::string> assignment(std::string_view value,const char* option){
  const auto separator=value.find('=');
  if(separator==std::string_view::npos||separator==0||separator+1==value.size())
    throw std::invalid_argument(std::string(option)+" expects NUMBER=VALUE");
  std::uint32_t number{};const auto first=value.data();const auto last=first+separator;
  const auto parsed=std::from_chars(first,last,number);
  if(parsed.ec!=std::errc{}||parsed.ptr!=last)throw std::invalid_argument(std::string(option)+" has an invalid number");
  return {number,std::string(value.substr(separator+1))};
}
std::uint32_t unsigned_number(std::string_view value,const char* option){
  std::uint32_t number{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
  if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())throw std::invalid_argument(std::string(option)+" has an invalid slot ID");
  return number;
}
}

int main(int argc,char** argv){
  try{
    std::string wav,audio_name,midi_name,script="examples/generate.lua",timeline_file;
    std::vector<std::pair<std::uint32_t,std::filesystem::path>> slot_files;
    std::vector<std::pair<std::uint8_t,std::uint32_t>> note_maps;
    bool list=false,headless=false,interactive=false;
    for(int i=1;i<argc;++i){
      const std::string arg=argv[i];
      if(arg=="--audio"&&i+1<argc)audio_name=argv[++i];
      else if(arg=="--midi"&&i+1<argc)midi_name=argv[++i];
      else if(arg=="--script"&&i+1<argc)script=argv[++i];
      else if(arg=="--timeline"&&i+1<argc)timeline_file=argv[++i];
      else if(arg=="--slot"&&i+1<argc){auto [id,path]=assignment(argv[++i],"--slot");slot_files.emplace_back(id,path);}
      else if(arg=="--map"&&i+1<argc){auto [note,id_text]=assignment(argv[++i],"--map");if(note>127)throw std::invalid_argument("--map note must be 0..127");note_maps.emplace_back(static_cast<std::uint8_t>(note),unsigned_number(id_text,"--map"));}
      else if(arg=="--list-devices")list=true;
      else if(arg=="--headless")headless=true;
      else if(arg=="--interactive")interactive=true;
      else if(arg=="--help"){usage();return 0;}
      else if(!arg.empty()&&arg[0]!='-'&&wav.empty())wav=arg;
      else{usage();return 2;}
    }

    neurogenic::AudioEngine engine;neurogenic::SampleManager samples(engine);neurogenic::MidiMapper mapper;neurogenic::ControlDispatcher dispatcher(mapper);
    neurogenic::AudioDevice audio(engine,headless);neurogenic::MidiInput midi(mapper);
    if(list){
      std::cout<<"Audio backend: "<<audio.backend()<<"\nAudio outputs:\n";std::size_t index=0;
      for(const auto& name:audio.outputs())std::cout<<"  ["<<index++<<"] "<<name<<'\n';if(index==0)std::cout<<"  (none discovered)\n";
      std::cout<<"MIDI backend: "<<midi.backend()<<"\nMIDI inputs:\n";index=0;
      for(const auto& name:midi.inputs())std::cout<<"  ["<<index++<<"] "<<name<<'\n';if(index==0)std::cout<<"  (none discovered)\n";
      if(wav.empty()&&slot_files.empty()&&!interactive)return 0;
    }
    if(wav.empty()&&slot_files.empty()&&!interactive){usage();return 2;}

    if(slot_files.empty()&&!wav.empty())engine.set_sample(neurogenic::load_wav(wav,engine.sample_rate()));
    else{
      if(!wav.empty())slot_files.emplace_back(0,wav);
      for(const auto& [id,path]:slot_files){
        if(!samples.load(neurogenic::SampleSlotId{id},path))throw std::runtime_error("sample command queue is full");
      }
      samples.wait_idle();
      for(const auto& [id,path]:slot_files){(void)path;const auto state=samples.status(neurogenic::SampleSlotId{id});if(state.state==neurogenic::SampleSlotLoadState::error)throw std::runtime_error("slot "+std::to_string(id)+" failed: "+state.error);}
      for(const auto& [note,id]:note_maps)
        if(!engine.map_note_to_slot(note,neurogenic::SampleSlotId{id}))throw std::invalid_argument("--map references an unloaded sample slot");
      std::array<float,0> no_audio{};engine.process(no_audio,no_audio);
      for(const auto& [id,path]:slot_files){(void)path;print_status(samples.status(neurogenic::SampleSlotId{id}));}
    }

    neurogenic::ScriptRuntime lua([&](auto pattern){return engine.queue_pattern(pattern);},[&]{return engine.frame_position();});
    neurogenic::Pattern initial;for(auto step:{0U,4U,8U,12U})initial.steps[step]={true,36,1.0F};initial.revision=1;
    if(timeline_file.empty()){if(!engine.queue_pattern(initial))throw std::runtime_error("initial pattern queue failed");}
    else {auto timeline=neurogenic::load_timeline(timeline_file);std::string timeline_error;if(!engine.queue_timeline(timeline,timeline_error))throw std::runtime_error("timeline queue failed: "+timeline_error);}
    mapper.set_note_handler([&](auto note,auto velocity){engine.trigger(note,velocity);});
    mapper.map_button(0,36,"pattern.generate",[&]{std::string error;if(lua.execute_file(script,error))std::cout<<"Lua pattern queued for next bar\n";else std::cerr<<"Lua error: "<<error<<'\n';});
    dispatcher.start();std::string error;
    if(!audio.start(audio_name,error)){std::cerr<<"audio unavailable: "<<error<<'\n';return 1;}
    if(!midi.open(midi_name,error))std::cerr<<"MIDI unavailable (audio continues): "<<error<<'\n';else std::cout<<"MIDI input active\n";
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    std::cout<<"Playing "<<engine.sample_slot_count()<<" sample slot(s). MIDI note 36 runs "<<script<<". Press Ctrl-C to stop.\n";
    if(interactive){std::cout<<"Commands: load ID FILE | replace ID FILE | unload ID | status [ID] | quit\n";std::string line;while(running.load()&&std::getline(std::cin,line)){std::istringstream input(line);std::string command;input>>command;if(command=="quit"||command=="exit")break;if(command=="load"||command=="replace"){std::uint32_t id{};std::string path;input>>id>>std::ws;std::getline(input,path);if(path.empty()||!samples.load(neurogenic::SampleSlotId{id},path))std::cerr<<"sample command rejected\n";}else if(command=="unload"){std::uint32_t id{};if(!(input>>id)||!samples.unload(neurogenic::SampleSlotId{id}))std::cerr<<"unload command rejected\n";}else if(command=="status"){std::uint32_t id{};if(input>>id)print_status(samples.status(neurogenic::SampleSlotId{id}));else for(const auto& state:samples.states())print_status(state);}else std::cerr<<"unknown command\n";}}
    else while(running.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));
    audio.stop();midi.close();dispatcher.stop();samples.shutdown();
    std::cout<<"Stopped. dropped MIDI="<<mapper.dropped_messages()<<", triggers="<<engine.dropped_triggers()<<", patterns="<<engine.dropped_patterns()<<", timelines="<<engine.dropped_timelines()<<", timeline overruns="<<engine.timeline_event_overruns()<<", legacy events="<<engine.dropped_legacy_events()<<", transport commands="<<engine.dropped_transport_commands()<<", missing slots="<<engine.missing_slot_triggers()<<", stolen voices="<<engine.stolen_voices()<<'\n';
    return 0;
  }catch(const std::exception& error){std::cerr<<"error: "<<error.what()<<'\n';return 1;}
}
