#include "neurogenic/audio/sample.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace neurogenic {
namespace { template<class T> T read(std::istream& in) { T v{}; in.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; } }
Sample decode_wav(const std::filesystem::path& path) {
  const auto fail=[&](const char* message){throw std::runtime_error(path.string()+": "+message);};
  std::ifstream in(path, std::ios::binary); if (!in) fail("cannot open WAV");
  char riff[4]{},wave[4]{};in.read(riff,4);(void)read<std::uint32_t>(in);in.read(wave,4);
  if(!in||std::memcmp(riff,"RIFF",4)||std::memcmp(wave,"WAVE",4))fail("not a RIFF/WAVE file");
  std::uint16_t format=0, channels=0, bits=0; std::uint32_t rate=0; std::vector<char> bytes;
  while (in && (!format || bytes.empty())) { char id[4]; in.read(id,4); if(!in) break; auto size=read<std::uint32_t>(in);
    if(!std::memcmp(id,"fmt ",4)) { if(size<16)fail("invalid fmt chunk");format=read<std::uint16_t>(in); channels=read<std::uint16_t>(in); rate=read<std::uint32_t>(in); (void)read<std::uint32_t>(in); (void)read<std::uint16_t>(in); bits=read<std::uint16_t>(in); in.seekg(size-16, std::ios::cur); }
    else if(!std::memcmp(id,"data",4)) { bytes.resize(size); in.read(bytes.data(), size);if(!in)fail("truncated data chunk"); }
    else { in.seekg(size, std::ios::cur); }
    if(size & 1) in.seekg(1,std::ios::cur);
  }
  if((format!=1&&format!=3)||(bits!=16&&bits!=24&&bits!=32)||(format==3&&bits!=32)||(channels!=1&&channels!=2)||rate==0||bytes.empty())
    fail("supported WAV formats are non-empty mono/stereo PCM16, PCM24, PCM32, or float32");
  const auto width=static_cast<std::size_t>(bits/8);
  if(bytes.size()%(channels*width)!=0)fail("WAV data is not aligned to complete frames");
  Sample result{rate,channels,{}};result.interleaved.resize(bytes.size()/width);
  for(std::size_t i=0;i<result.interleaved.size();++i){const auto* p=reinterpret_cast<const unsigned char*>(bytes.data()+i*width);
    if(format==3){float value{};std::memcpy(&value,p,4);if(!std::isfinite(value))fail("WAV contains a non-finite float sample");result.interleaved[i]=value;}
    else if(bits==16){std::int16_t value{};std::memcpy(&value,p,2);result.interleaved[i]=value/32768.0F;}
    else if(bits==24){std::int32_t value=static_cast<std::int32_t>(p[0])|(static_cast<std::int32_t>(p[1])<<8)|(static_cast<std::int32_t>(p[2])<<16);if(value&0x800000)value|=~0xFFFFFF;result.interleaved[i]=value/8388608.0F;}
    else {std::int32_t value{};std::memcpy(&value,p,4);result.interleaved[i]=static_cast<float>(static_cast<double>(value)/2147483648.0);}
  }
  return result;
}

Sample WindowedSincConverter::convert(const Sample& source,std::uint32_t target_rate) const{
  if(source.sample_rate==0||target_rate==0)throw std::invalid_argument("sample rates must be positive");
  if((source.channels!=1&&source.channels!=2)||source.frames()==0)throw std::invalid_argument("resampling requires non-empty mono or stereo audio");
  if(source.sample_rate==target_rate)return source;
  const auto output_frames=std::max<std::size_t>(1,static_cast<std::size_t>(std::llround(static_cast<long double>(source.frames())*target_rate/source.sample_rate)));
  Sample output{target_rate,source.channels,std::vector<float>(output_frames*source.channels)};
  constexpr int radius=16;const auto ratio=static_cast<double>(target_rate)/source.sample_rate;const auto cutoff=std::min(1.0,ratio);
  constexpr double pi=3.1415926535897932384626433832795;
  for(std::size_t frame=0;frame<output_frames;++frame){const auto position=static_cast<double>(frame)*source.sample_rate/target_rate;const auto center=static_cast<long long>(std::floor(position));
    for(std::uint16_t channel=0;channel<source.channels;++channel){double sum{},weight_sum{};
      for(int tap=-radius+1;tap<=radius;++tap){const auto index=center+tap;if(index<0||index>=static_cast<long long>(source.frames()))continue;const auto distance=position-static_cast<double>(index);const auto normalized=distance/radius;const auto window=0.5*(1.0+std::cos(pi*normalized));const auto x=pi*distance*cutoff;const auto sinc=std::abs(x)<1e-12?1.0:std::sin(x)/x;const auto weight=cutoff*sinc*window;sum+=source.interleaved[static_cast<std::size_t>(index)*source.channels+channel]*weight;weight_sum+=weight;}
      output.interleaved[frame*source.channels+channel]=static_cast<float>(weight_sum==0?0:sum/weight_sum);
    }
  }
  return output;
}

Sample prepare_sample(Sample source,std::uint32_t target_rate,const SampleRateConverter& converter){
  if(source.sample_rate==0)throw std::invalid_argument("source sample rate must be positive");
  if(target_rate==0)throw std::invalid_argument("engine sample rate must be positive");
  if((source.channels!=1&&source.channels!=2)||source.interleaved.empty()||source.interleaved.size()%source.channels!=0)throw std::invalid_argument("sample must contain complete mono or stereo frames");
  if(source.sample_rate==target_rate)return source;
  return converter.convert(source,target_rate);
}

Sample load_wav(const std::filesystem::path& path,std::uint32_t target_rate,const SampleRateConverter* converter){
  auto decoded=decode_wav(path);if(target_rate==0||target_rate==decoded.sample_rate)return decoded;
  WindowedSincConverter fallback;return prepare_sample(std::move(decoded),target_rate,converter?*converter:fallback);
}
}
