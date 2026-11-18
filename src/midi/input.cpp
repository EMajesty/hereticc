#include "neurogenic/midi/input.hpp"
#ifdef NEUROGENIC_HAS_LIBREMIDI
#include <libremidi/libremidi.hpp>
#endif
#include <algorithm>
namespace neurogenic {
struct MidiInput::Impl { MidiMapper& mapper;
#ifdef NEUROGENIC_HAS_LIBREMIDI
  libremidi::observer observer;std::unique_ptr<libremidi::midi_in> input;
#endif
  explicit Impl(MidiMapper& m):mapper(m){}
};
MidiInput::MidiInput(MidiMapper& m):impl_(std::make_unique<Impl>(m)){}
MidiInput::~MidiInput()=default;
std::vector<std::string> MidiInput::inputs() const {std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_LIBREMIDI
for(const auto& port:impl_->observer.get_input_ports())result.push_back(port.display_name.empty()?port.port_name:port.display_name);
#endif
return result;}
bool MidiInput::open(const std::string& selector,std::string& error){
#ifdef NEUROGENIC_HAS_LIBREMIDI
try{auto ports=impl_->observer.get_input_ports();auto selected=ports.end();if(selector.empty()){if(!ports.empty())selected=ports.begin();}else{const bool numeric=std::all_of(selector.begin(),selector.end(),[](unsigned char c){return c>='0'&&c<='9';});if(numeric){const auto index=std::stoul(selector);if(index<ports.size())selected=ports.begin()+static_cast<std::ptrdiff_t>(index);}else selected=std::find_if(ports.begin(),ports.end(),[&](const auto& p){return p.port_name.find(selector)!=std::string::npos||p.display_name.find(selector)!=std::string::npos;});}if(selected==ports.end()){error=ports.empty()?"no MIDI inputs available":"MIDI input not found: "+selector;return false;}libremidi::input_configuration config;config.on_message=[this](const libremidi::message& message){if(message.size()>=3)impl_->mapper.receive({message[0],message[1],message[2],static_cast<std::uint64_t>(message.timestamp)});};impl_->input=std::make_unique<libremidi::midi_in>(config);auto status=impl_->input->open_port(*selected);if(status!=stdx::error{})status.throw_exception();return true;}catch(const std::exception& e){error=e.what();impl_->input.reset();return false;}
#else
(void)selector;error="libremidi support was not built";return false;
#endif
}
}
