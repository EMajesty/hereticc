#include "neurogenic/midi/input.hpp"
#ifdef NEUROGENIC_HAS_LIBREMIDI
#include <libremidi/libremidi.hpp>
#endif
#include <algorithm>
namespace neurogenic {
namespace {
#ifdef NEUROGENIC_HAS_LIBREMIDI
bool alsa_compiled(){
#if defined(LIBREMIDI_ALSA)
return true;
#else
return false;
#endif
}
#endif
}
struct MidiInput::Impl { MidiMapper& mapper;
#ifdef NEUROGENIC_HAS_LIBREMIDI
  libremidi::observer observer;std::unique_ptr<libremidi::midi_in> input;
#endif
  explicit Impl(MidiMapper& m):mapper(m){}
};
MidiInput::MidiInput(MidiMapper& m):impl_(std::make_unique<Impl>(m)){}
MidiInput::~MidiInput()=default;
std::string MidiInput::backend() const{
#ifdef NEUROGENIC_HAS_LIBREMIDI
const auto api=impl_->observer.get_current_api();if(api==libremidi::API::DUMMY)return alsa_compiled()?"ALSA sequencer compiled, but unavailable at runtime":"not compiled (ALSA sequencer backend missing)";return std::string(libremidi::get_api_display_name(api));
#else
return "not compiled (configure with NEUROGENIC_FETCH_DEPENDENCIES=ON)";
#endif
}
std::vector<std::string> MidiInput::inputs() const {std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_LIBREMIDI
for(const auto& port:impl_->observer.get_input_ports())result.push_back(port.display_name.empty()?port.port_name:port.display_name);
#endif
return result;}
bool MidiInput::open(const std::string& selector,std::string& error){
#ifdef NEUROGENIC_HAS_LIBREMIDI
try{if(impl_->observer.get_current_api()==libremidi::API::DUMMY){error=alsa_compiled()?"ALSA sequencer backend was compiled, but is unavailable (is /dev/snd/seq accessible?)":"libremidi was compiled without the ALSA sequencer backend";return false;}auto ports=impl_->observer.get_input_ports();auto selected=ports.end();if(selector.empty()){if(!ports.empty())selected=ports.begin();}else{const bool numeric=std::all_of(selector.begin(),selector.end(),[](unsigned char c){return c>='0'&&c<='9';});if(numeric){const auto index=std::stoul(selector);if(index<ports.size())selected=ports.begin()+static_cast<std::ptrdiff_t>(index);}else selected=std::find_if(ports.begin(),ports.end(),[&](const auto& p){return p.port_name.find(selector)!=std::string::npos||p.display_name.find(selector)!=std::string::npos;});}if(selected==ports.end()){error=ports.empty()?"ALSA sequencer backend is active, but no MIDI inputs were discovered":"ALSA sequencer backend found no MIDI input matching: "+selector;return false;}libremidi::input_configuration config;config.on_message=[this](const libremidi::message& message){if(message.size()>=3)impl_->mapper.receive({message[0],message[1],message[2],static_cast<std::uint64_t>(message.timestamp)});};impl_->input=std::make_unique<libremidi::midi_in>(config);auto status=impl_->input->open_port(*selected);if(status!=stdx::error{})status.throw_exception();return true;}catch(const std::exception& e){error="MIDI device was discovered but could not be opened: "+std::string(e.what());impl_->input.reset();return false;}
#else
(void)selector;error="libremidi support was not built";return false;
#endif
}
}
