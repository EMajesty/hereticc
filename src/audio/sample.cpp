#include "neurogenic/audio/sample.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace neurogenic {
namespace { template<class T> T read(std::istream& in) { T v{}; in.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; } }
Sample load_wav(const std::filesystem::path& path) {
  const auto fail=[&](const char* message){throw std::runtime_error(path.string()+": "+message);};
  std::ifstream in(path, std::ios::binary); if (!in) fail("cannot open WAV");
  char riff[4]{},wave[4]{};in.read(riff,4);(void)read<std::uint32_t>(in);in.read(wave,4);
  if(!in||std::memcmp(riff,"RIFF",4)||std::memcmp(wave,"WAVE",4))fail("not a RIFF/WAVE file");
  std::uint16_t format=0, channels=0, bits=0; std::uint32_t rate=0; std::vector<char> bytes;
  while (in && (!format || bytes.empty())) { char id[4]; in.read(id,4); if(!in) break; auto size=read<std::uint32_t>(in);
    if(!std::memcmp(id,"fmt ",4)) { if(size<16)fail("invalid fmt chunk");format=read<std::uint16_t>(in); channels=read<std::uint16_t>(in); rate=read<std::uint32_t>(in); (void)read<std::uint32_t>(in); (void)read<std::uint16_t>(in); bits=read<std::uint16_t>(in); in.seekg(size-16, std::ios::cur); }
    else if(!std::memcmp(id,"data",4)) { bytes.resize(size); in.read(bytes.data(), size); }
    else { in.seekg(size, std::ios::cur); }
    if(size & 1) in.seekg(1,std::ios::cur);
  }
  if(format!=1||bits!=16||(channels!=1&&channels!=2)||rate==0||bytes.empty()||bytes.size()%(channels*2)!=0)fail("only non-empty mono or stereo PCM16 WAV is supported");
  Sample result{rate,channels,{}}; result.interleaved.resize(bytes.size()/2);
  for(std::size_t i=0;i<result.interleaved.size();++i) { std::int16_t v; std::memcpy(&v,bytes.data()+i*2,2); result.interleaved[i]=v/32768.0F; }
  return result;
}
}
