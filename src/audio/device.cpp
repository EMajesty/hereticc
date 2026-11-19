#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#ifdef NEUROGENIC_HAS_MINIAUDIO
#include <miniaudio.h>
#endif
#ifdef NEUROGENIC_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
#include <algorithm>
#include <array>
namespace neurogenic {
struct AudioDevice::Impl {
  AudioEngine& engine;
  bool force_null{};
#ifdef NEUROGENIC_HAS_MINIAUDIO
  ma_context context{};ma_device device{};bool context_ready{},device_ready{};
  std::uint16_t input_channels{};
  static void callback(ma_device* device,void* output,const void* input,ma_uint32 count){auto& self=*static_cast<Impl*>(device->pUserData);auto* out=static_cast<float*>(output);const auto* in=static_cast<const float*>(input);std::array<float,1024> left{},right{};ma_uint32 done=0;while(done<count){auto n=std::min<ma_uint32>(1024,count-done);if(in&&self.input_channels)self.engine.process_duplex({left.data(),n},{right.data(),n},{in+static_cast<std::size_t>(done)*self.input_channels,static_cast<std::size_t>(n)*self.input_channels},self.input_channels);else self.engine.process({left.data(),n},{right.data(),n});for(ma_uint32 i=0;i<n;++i){out[(done+i)*2]=left[i];out[(done+i)*2+1]=right[i];}done+=n;}}
#endif
  explicit Impl(AudioEngine& e,bool null_backend):engine(e),force_null(null_backend){}
};
AudioDevice::AudioDevice(AudioEngine& e,bool force_null):impl_(std::make_unique<Impl>(e,force_null)){
#ifdef NEUROGENIC_HAS_MINIAUDIO
  const ma_backend requested=ma_backend_null;
  impl_->context_ready=ma_context_init(force_null?&requested:nullptr,force_null?1U:0U,nullptr,&impl_->context)==MA_SUCCESS;
#else
  (void)force_null;
#endif
}
AudioDevice::~AudioDevice(){stop();
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->context_ready)ma_context_uninit(&impl_->context);
#endif
}
std::string AudioDevice::backend() const{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(!impl_->context_ready)return "compiled, but no backend could be initialized";if(impl_->context.backend==ma_backend_null){
#ifdef NEUROGENIC_HAS_ALSA
if(impl_->force_null)return "Null (explicit headless selection; ALSA "+std::string(snd_asoundlib_version())+" linked)";
#else
if(impl_->force_null)return "Null (explicit headless selection)";
#endif
ma_backend enabled[MA_BACKEND_COUNT]{};size_t count{};if(ma_get_enabled_backends(enabled,MA_BACKEND_COUNT,&count)==MA_SUCCESS&&std::find(enabled,enabled+count,ma_backend_alsa)!=enabled+count){
#ifdef NEUROGENIC_HAS_ALSA
return "Null (ALSA "+std::string(snd_asoundlib_version())+" linked, but backend unavailable)";
#else
return "Null (ALSA compiled, but runtime library unavailable)";
#endif
}}return ma_get_backend_name(impl_->context.backend);
#else
return "not compiled (configure with NEUROGENIC_FETCH_DEPENDENCIES=ON)";
#endif
}
std::vector<std::string> AudioDevice::outputs() const{std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(!impl_->context_ready)return result;ma_device_info* playback{};ma_uint32 count{};if(ma_context_get_devices(&impl_->context,&playback,&count,nullptr,nullptr)==MA_SUCCESS)for(ma_uint32 i=0;i<count;++i)result.emplace_back(playback[i].name);
#endif
return result;}
std::vector<std::string> AudioDevice::inputs() const{std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(!impl_->context_ready)return result;ma_device_info* capture{};ma_uint32 count{};if(ma_context_get_devices(&impl_->context,nullptr,nullptr,&capture,&count)==MA_SUCCESS)for(ma_uint32 i=0;i<count;++i)result.emplace_back(capture[i].name);
#endif
return result;}
bool AudioDevice::start(const std::string& selector,std::string& error){
return start_duplex(selector,"",0,error);
}
bool AudioDevice::start_duplex(const std::string& selector,const std::string& input_selector,std::uint16_t input_channels,std::string& error){
#ifdef NEUROGENIC_HAS_MINIAUDIO
 if(!impl_->context_ready){error="miniaudio was compiled, but no audio backend could be initialized";return false;}if(input_channels>2){error="initial duplex support is limited to mono or stereo input";return false;}ma_device_id* selected=nullptr;ma_device_id* selected_input=nullptr;ma_device_info* playback{};ma_device_info* capture{};ma_uint32 count{},capture_count{};if(ma_context_get_devices(&impl_->context,&playback,&count,&capture,&capture_count)!=MA_SUCCESS){error="audio backend '"+backend()+"' is available, but device enumeration failed";return false;}auto select=[&](const std::string& name,ma_device_info* devices,ma_uint32 size)->ma_device_id*{if(name.empty())return nullptr;const bool numeric=std::all_of(name.begin(),name.end(),[](unsigned char c){return c>='0'&&c<='9';});if(numeric){const auto index=std::stoul(name);return index<size?&devices[index].id:nullptr;}for(ma_uint32 i=0;i<size;++i)if(std::string(devices[i].name).find(name)!=std::string::npos)return &devices[i].id;return nullptr;};if(!selector.empty()&&!(selected=select(selector,playback,count))){error="audio backend '"+backend()+"' found no output matching: "+selector;return false;}if(input_channels&& !input_selector.empty()&&!(selected_input=select(input_selector,capture,capture_count))){error="audio backend '"+backend()+"' found no input matching: "+input_selector;return false;}auto config=ma_device_config_init(input_channels?ma_device_type_duplex:ma_device_type_playback);config.playback.format=ma_format_f32;config.playback.channels=2;config.playback.pDeviceID=selected;if(input_channels){config.capture.format=ma_format_f32;config.capture.channels=input_channels;config.capture.pDeviceID=selected_input;}config.sampleRate=48000;config.dataCallback=&Impl::callback;config.pUserData=impl_.get();auto status=ma_device_init(&impl_->context,&config,&impl_->device);if(status!=MA_SUCCESS){error="audio backend '"+backend()+"' found the device but could not open "+std::string(input_channels?"duplex":"playback")+": "+ma_result_description(status);return false;}impl_->input_channels=input_channels;impl_->device_ready=true;status=ma_device_start(&impl_->device);if(status!=MA_SUCCESS){error="audio device opened but could not start: "+std::string(ma_result_description(status));stop();return false;}return true;
#else
(void)selector;(void)input_selector;(void)input_channels;error="miniaudio support was not built";return false;
#endif
}
void AudioDevice::stop() noexcept{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->device_ready){ma_device_uninit(&impl_->device);impl_->device_ready=false;impl_->engine.recording_device_stopped();}
#endif
}
}
