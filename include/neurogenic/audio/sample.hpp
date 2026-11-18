#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <vector>
namespace neurogenic {
struct SampleSlotId {
  std::uint32_t value{};
  friend constexpr bool operator==(SampleSlotId,SampleSlotId)=default;
};
inline constexpr auto invalid_sample_slot=SampleSlotId{std::numeric_limits<std::uint32_t>::max()};
struct Sample {
  std::uint32_t sample_rate{};
  std::uint16_t channels{};
  std::vector<float> interleaved;
  [[nodiscard]] std::size_t frames() const noexcept{return channels==0?0:interleaved.size()/channels;}
};
class SampleRateConverter {
public:
  virtual ~SampleRateConverter()=default;
  [[nodiscard]] virtual Sample convert(const Sample& source,std::uint32_t target_rate) const=0;
};
class WindowedSincConverter final:public SampleRateConverter {
public:[[nodiscard]] Sample convert(const Sample& source,std::uint32_t target_rate) const override;
};
Sample decode_wav(const std::filesystem::path& path);
Sample prepare_sample(Sample source,std::uint32_t target_rate,const SampleRateConverter& converter);
Sample load_wav(const std::filesystem::path& path,std::uint32_t target_rate=0,const SampleRateConverter* converter=nullptr);
}
