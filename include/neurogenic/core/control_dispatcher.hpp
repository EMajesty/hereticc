#pragma once
#include <atomic>
#include <thread>

namespace neurogenic {
class MidiMapper;
class ControlDispatcher {
public:
  explicit ControlDispatcher(MidiMapper& mapper):mapper_(mapper){}
  ~ControlDispatcher();
  ControlDispatcher(const ControlDispatcher&)=delete;
  ControlDispatcher& operator=(const ControlDispatcher&)=delete;
  void start();
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept{return running_.load(std::memory_order_acquire);}
private:
  void run();
  MidiMapper& mapper_;
  std::atomic<bool> stopping_{},running_{};
  std::thread worker_;
};
}
