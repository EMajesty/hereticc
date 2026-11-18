#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#ifdef NEUROGENIC_HAS_MINIAUDIO
#include <miniaudio.h>
#endif
#include <algorithm>
#include <array>
namespace neurogenic {
struct AudioDevice::Impl {
  AudioEngine& engine;
#ifdef NEUROGENIC_HAS_MINIAUDIO
  ma_context context{};ma_device device{};bool context_ready{},device_ready{};
  static void callback(ma_device* device,void* output,const void*,ma_uint32 count){auto& self=*static_cast<Impl*>(device->pUserData);auto* out=static_cast<float*>(output);std::array<float,1024> left{},right{};ma_uint32 done=0;while(done<count){auto n=std::min<ma_uint32>(1024,count-done);self.engine.process({left.data(),n},{right.data(),n});for(ma_uint32 i=0;i<n;++i){out[(done+i)*2]=left[i];out[(done+i)*2+1]=right[i];}done+=n;}}
#endif
  explicit Impl(AudioEngine& e):engine(e){}
};
AudioDevice::AudioDevice(AudioEngine& e):impl_(std::make_unique<Impl>(e)){
#ifdef NEUROGENIC_HAS_MINIAUDIO
  impl_->context_ready=ma_context_init(nullptr,0,nullptr,&impl_->context)==MA_SUCCESS;
#endif
}
AudioDevice::~AudioDevice(){stop();
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->context_ready)ma_context_uninit(&impl_->context);
#endif
}
std::vector<std::string> AudioDevice::outputs() const{std::vector<std::string> result;
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(!impl_->context_ready)return result;ma_device_info* playback{};ma_uint32 count{};if(ma_context_get_devices(&impl_->context,&playback,&count,nullptr,nullptr)==MA_SUCCESS)for(ma_uint32 i=0;i<count;++i)result.emplace_back(playback[i].name);
#endif
return result;}
bool AudioDevice::start(const std::string& selector,std::string& error){
#ifdef NEUROGENIC_HAS_MINIAUDIO
 if(!impl_->context_ready){error="miniaudio context initialization failed";return false;}ma_device_id* selected=nullptr;ma_device_info* playback{};ma_uint32 count{};if(!selector.empty()){if(ma_context_get_devices(&impl_->context,&playback,&count,nullptr,nullptr)!=MA_SUCCESS){error="audio device enumeration failed";return false;}const bool numeric=std::all_of(selector.begin(),selector.end(),[](unsigned char c){return c>='0'&&c<='9';});if(numeric){const auto index=std::stoul(selector);if(index<count)selected=&playback[index].id;}else for(ma_uint32 i=0;i<count;++i)if(std::string(playback[i].name).find(selector)!=std::string::npos){selected=&playback[i].id;break;}if(!selected){error="audio output not found: "+selector;return false;}}auto config=ma_device_config_init(ma_device_type_playback);config.playback.format=ma_format_f32;config.playback.channels=2;config.playback.pDeviceID=selected;config.sampleRate=48000;config.dataCallback=&Impl::callback;config.pUserData=impl_.get();auto status=ma_device_init(&impl_->context,&config,&impl_->device);if(status!=MA_SUCCESS){error=ma_result_description(status);return false;}impl_->device_ready=true;status=ma_device_start(&impl_->device);if(status!=MA_SUCCESS){error=ma_result_description(status);stop();return false;}return true;
#else
(void)selector;error="miniaudio support was not built";return false;
#endif
}
void AudioDevice::stop() noexcept{
#ifdef NEUROGENIC_HAS_MINIAUDIO
if(impl_->device_ready){ma_device_uninit(&impl_->device);impl_->device_ready=false;}
#endif
}
}
