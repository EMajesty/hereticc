#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include <iostream>
int main(int argc,char**argv){try{neurogenic::AudioEngine engine; if(argc>1)engine.set_sample(neurogenic::load_wav(argv[1]));else std::cout<<"No WAV supplied; running silent/offline-safe mode.\n";neurogenic::ScriptRuntime lua([&](auto p){engine.scheduler().request_pattern(p);});neurogenic::MidiMapper midi;midi.map_button(0,36,"pattern.generate",[&]{std::string error;lua.execute("pattern x...x...x.x...x.",error);std::cout<<"Generated rhythm queued for next bar.\n";});midi.receive({0x90,36,100,0});engine.render_wav("neurogenic-demo.wav",48000*4);std::cout<<"NEUROGENIC v0.1: rendered neurogenic-demo.wav (simulated MIDI, 120 BPM).\n";return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
