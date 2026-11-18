#pragma once
#include <array>
#include <atomic>
#include <cstddef>
namespace neurogenic {
template <typename T, std::size_t Capacity> class SpscQueue {
  static_assert(Capacity > 1);
public:
  bool push(const T& value) noexcept { const auto w=write_.load(std::memory_order_relaxed); const auto n=next(w); if(n==read_.load(std::memory_order_acquire))return false; storage_[w]=value; write_.store(n,std::memory_order_release); return true; }
  bool pop(T& value) noexcept { const auto r=read_.load(std::memory_order_relaxed); if(r==write_.load(std::memory_order_acquire))return false; value=storage_[r]; read_.store(next(r),std::memory_order_release); return true; }
  static constexpr std::size_t usable_capacity=Capacity-1;
private:
  static constexpr std::size_t next(std::size_t value) noexcept{return(value+1)%Capacity;}
  std::array<T,Capacity> storage_{}; alignas(64) std::atomic<std::size_t> write_{}; alignas(64) std::atomic<std::size_t> read_{};
};
}
