#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#ifdef NEUROGENIC_HAS_MINIAUDIO
#include <miniaudio.h>
#endif
#ifdef NEUROGENIC_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
#include <algorithm>
#include <atomic>
#include <array>
namespace neurogenic {
struct AudioDevice::Impl {
  AudioEngine& engine;
  bool force_null{};
  std::uint16_t input_channels{},output_channels{};
  std::uint32_t sample_rate{},buffer_frames{};
  std::atomic<bool> running{};
  std::atomic<std::uint64_t> starts{},stops{},reroutes{},interruptions{};
#ifdef NEUROGENIC_HAS_MINIAUDIO
  ma_context context{};ma_device device{};bool context_ready{},device_ready{};
  static void callback(ma_device* device,void* output,const void* input,ma_uint32 count){auto& self=*static_cast<Impl*>(device->pUserData);auto* out=static_cast<float*>(output);const auto* in=static_cast<const float*>(input);ma_uint32 done=0;while(done<count){auto n=std::min<ma_uint32>(1024,count-done);self.engine.process_io({out+static_cast<std::size_t>(done)*self.output_channels,static_cast<std::size_t>(n)*self.output_channels},self.output_channels,in?std::span<const float>{in+static_cast<std::size_t>(done)*self.input_channels,static_cast<std::size_t>(n)*self.input_channels}:std::span<const float>{},self.input_channels);done+=n;}}
  static void notification(const ma_device_notification* event){if(!event||!event->pDevice||!event->pDevice->pUserData)return;auto& self=*static_cast<Impl*>(event->pDevice->pUserData);switch(event->type){case ma_device_notification_type_started:self.running.store(true,std::memory_order_release);self.starts.fetch_add(1,std::memory_order_relaxed);break;case ma_device_notification_type_stopped:self.running.store(false,std::memory_order_release);self.stops.fetch_add(1,std::memory_order_relaxed);break;case ma_device_notification_type_rerouted:self.reroutes.fetch_add(1,std::memory_order_relaxed);break;case ma_device_notification_type_interruption_began:self.running.store(false,std::memory_order_release);self.interruptions.fetch_add(1,std::memory_order_relaxed);break;case ma_device_notification_type_interruption_ended:self.interruptions.fetch_add(1,std::memory_order_relaxed);break;default:break;}}
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
namespace {
#ifdef NEUROGENIC_HAS_MINIAUDIO
std::vector<AudioPortCapabilities> capabilities(ma_context &context,
                                                ma_device_type type) {
 std::vector<AudioPortCapabilities> result;ma_device_info* playback{};ma_device_info* capture{};ma_uint32 playback_count{},capture_count{};if(ma_context_get_devices(&context,&playback,&playback_count,&capture,&capture_count)!=MA_SUCCESS)return result;auto* devices=type==ma_device_type_playback?playback:capture;const auto count=type==ma_device_type_playback?playback_count:capture_count;for(ma_uint32 i=0;i<count;++i){ma_device_info full{};AudioPortCapabilities port;port.name=devices[i].name;if(ma_context_get_device_info(&context,type,&devices[i].id,&full)==MA_SUCCESS)for(ma_uint32 f=0;f<full.nativeDataFormatCount;++f){const auto channels=static_cast<std::uint16_t>(full.nativeDataFormats[f].channels);port.maximum_channels=std::max(port.maximum_channels,channels);AudioPortCapabilities::NativeFormat format{full.nativeDataFormats[f].sampleRate,channels};if(format.sample_rate&&format.channels&&std::find(port.native_formats.begin(),port.native_formats.end(),format)==port.native_formats.end())port.native_formats.push_back(format);}result.push_back(std::move(port));}return result;
}
#endif
}
std::vector<AudioPortCapabilities> AudioDevice::output_capabilities() const{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->context_ready)return capabilities(impl_->context,ma_device_type_playback);
#endif
return {};}
std::vector<AudioPortCapabilities> AudioDevice::input_capabilities() const{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->context_ready)return capabilities(impl_->context,ma_device_type_capture);
#endif
return {};}
bool AudioDevice::start(const std::string& selector,std::string& error,std::uint32_t preferred_buffer_frames){
return start_duplex(selector,"",0,error,2,preferred_buffer_frames);
}
bool AudioDevice::start_duplex(const std::string& selector,const std::string& input_selector,std::uint16_t input_channels,std::string& error,std::uint16_t output_channels,std::uint32_t preferred_buffer_frames){
#ifdef NEUROGENIC_HAS_MINIAUDIO
 if(!impl_->context_ready){error="miniaudio was compiled, but no audio backend could be initialized";return false;}if(input_channels>6||output_channels==0||output_channels>6){error="audio routing supports one to six input/output channels";return false;}if(preferred_buffer_frames>8192){error="preferred audio buffer must be 1..8192 frames, or zero for backend default";return false;}ma_device_id* selected=nullptr;ma_device_id* selected_input=nullptr;ma_device_info* playback{};ma_device_info* capture{};ma_uint32 count{},capture_count{};if(ma_context_get_devices(&impl_->context,&playback,&count,&capture,&capture_count)!=MA_SUCCESS){error="audio backend '"+backend()+"' is available, but device enumeration failed";return false;}auto select=[&](const std::string& name,ma_device_info* devices,ma_uint32 size)->ma_device_id*{if(name.empty())return nullptr;const bool numeric=std::all_of(name.begin(),name.end(),[](unsigned char c){return c>='0'&&c<='9';});if(numeric){const auto index=std::stoul(name);return index<size?&devices[index].id:nullptr;}for(ma_uint32 i=0;i<size;++i)if(std::string(devices[i].name).find(name)!=std::string::npos)return &devices[i].id;return nullptr;};if(!selector.empty()&&!(selected=select(selector,playback,count))){error="audio backend '"+backend()+"' found no output matching: "+selector;return false;}if(input_channels&& !input_selector.empty()&&!(selected_input=select(input_selector,capture,capture_count))){error="audio backend '"+backend()+"' found no input matching: "+input_selector;return false;}auto config=ma_device_config_init(input_channels?ma_device_type_duplex:ma_device_type_playback);config.playback.format=ma_format_f32;config.playback.channels=output_channels;config.playback.pDeviceID=selected;if(input_channels){config.capture.format=ma_format_f32;config.capture.channels=input_channels;config.capture.pDeviceID=selected_input;}config.sampleRate=48000;config.periodSizeInFrames=preferred_buffer_frames;config.performanceProfile=ma_performance_profile_low_latency;config.dataCallback=&Impl::callback;config.notificationCallback=&Impl::notification;config.pUserData=impl_.get();auto status=ma_device_init(&impl_->context,&config,&impl_->device);if(status!=MA_SUCCESS){error="audio backend '"+backend()+"' found the device but could not open "+std::string(input_channels?"duplex":"playback")+": "+ma_result_description(status);return false;}impl_->input_channels=input_channels;impl_->output_channels=output_channels;impl_->sample_rate=impl_->device.sampleRate;impl_->buffer_frames=impl_->device.playback.internalPeriodSizeInFrames;impl_->device_ready=true;if(impl_->sample_rate!=48000){error="audio device negotiated "+std::to_string(impl_->sample_rate)+" Hz; NEUROGENIC requires 48000 Hz and will not silently resample the device stream";stop();return false;}status=ma_device_start(&impl_->device);if(status!=MA_SUCCESS){error="audio device opened but could not start: "+std::string(ma_result_description(status));stop();return false;}error.clear();return true;
#else
(void)selector;(void)input_selector;(void)input_channels;(void)output_channels;error="miniaudio support was not built";return false;
#endif
}
void AudioDevice::stop() noexcept{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->device_ready){ma_device_uninit(&impl_->device);impl_->device_ready=false;impl_->engine.recording_device_stopped();}
#endif
}
std::uint32_t AudioDevice::selected_sample_rate() const noexcept{return impl_->sample_rate;}
std::uint32_t AudioDevice::selected_buffer_frames() const noexcept{return impl_->buffer_frames;}
std::uint16_t AudioDevice::selected_input_channels() const noexcept{return impl_->input_channels;}
std::uint16_t AudioDevice::selected_output_channels() const noexcept{return impl_->output_channels;}
AudioDeviceStatus AudioDevice::status() const noexcept{return {impl_->running.load(std::memory_order_acquire),false,0,impl_->starts.load(std::memory_order_relaxed),impl_->stops.load(std::memory_order_relaxed),impl_->reroutes.load(std::memory_order_relaxed),impl_->interruptions.load(std::memory_order_relaxed)};}
}
