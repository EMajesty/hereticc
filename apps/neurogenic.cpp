#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
namespace {std::atomic<bool> running{true};void stop(int){running=false;}void usage(){std::cout<<"usage: neurogenic [--audio NAME] [--midi NAME] [--script FILE] [--list-devices] SAMPLE.wav\n";}}
int main(int argc,char**argv){
 try{std::string wav,audio_name,midi_name,script="examples/generate.lua";bool list=false;for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--audio"&&i+1<argc)audio_name=argv[++i];else if(arg=="--midi"&&i+1<argc)midi_name=argv[++i];else if(arg=="--script"&&i+1<argc)script=argv[++i];else if(arg=="--list-devices")list=true;else if(arg=="--help"){usage();return 0;}else if(!arg.empty()&&arg[0]!='-')wav=arg;else{usage();return 2;}}
  neurogenic::AudioEngine engine;neurogenic::MidiMapper mapper;neurogenic::AudioDevice audio(engine);neurogenic::MidiInput midi(mapper);
  if(list){std::cout<<"Audio backend: "<<audio.backend()<<"\nAudio outputs:\n";std::size_t index=0;for(const auto& name:audio.outputs())std::cout<<"  ["<<index++<<"] "<<name<<'\n';if(index==0)std::cout<<"  (none discovered)\n";std::cout<<"MIDI backend: "<<midi.backend()<<"\nMIDI inputs:\n";index=0;for(const auto& name:midi.inputs())std::cout<<"  ["<<index++<<"] "<<name<<'\n';if(index==0)std::cout<<"  (none discovered)\n";if(wav.empty())return 0;}
  if(wav.empty()){usage();return 2;}auto sample=neurogenic::load_wav(wav);if(sample.sample_rate!=48000)std::cerr<<"warning: sample rate is "<<sample.sample_rate<<" Hz; resampling is not implemented (use 48000 Hz)\n";engine.set_sample(std::move(sample));
  neurogenic::ScriptRuntime lua([&](auto pattern){return engine.queue_pattern(pattern);},[&]{return engine.frame_position();});
  neurogenic::Pattern initial;for(auto step:{0U,4U,8U,12U})initial.steps[step]={true,60,1.0F};initial.revision=1;if(!engine.queue_pattern(initial))throw std::runtime_error("initial pattern queue failed");
  mapper.set_note_handler([&](auto note,auto velocity){engine.trigger(note,velocity);});mapper.map_button(0,36,"pattern.generate",[&]{std::string error;if(lua.execute_file(script,error))std::cout<<"Lua pattern queued for next bar\n";else std::cerr<<"Lua error: "<<error<<'\n';});
  std::string error;if(!audio.start(audio_name,error)){std::cerr<<"audio unavailable: "<<error<<'\n';return 1;}if(!midi.open(midi_name,error))std::cerr<<"MIDI unavailable (audio continues): "<<error<<'\n';else std::cout<<"MIDI input active\n";
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);std::cout<<"Playing. MIDI note 36 runs "<<script<<". Press Ctrl-C to stop.\n";while(running.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));audio.stop();std::cout<<"Stopped. dropped triggers="<<engine.dropped_triggers()<<", patterns="<<engine.dropped_patterns()<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
