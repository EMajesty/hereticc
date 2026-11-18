#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/midi/mapping.hpp"

namespace neurogenic {
ControlDispatcher::~ControlDispatcher(){stop();}

void ControlDispatcher::start(){
  if(worker_.joinable())return;
  stopping_.store(false,std::memory_order_release);
  worker_=std::thread(&ControlDispatcher::run,this);
}

void ControlDispatcher::stop() noexcept {
  if(!worker_.joinable())return;
  stopping_.store(true,std::memory_order_release);
  mapper_.sequence_.fetch_add(1,std::memory_order_release);
  mapper_.sequence_.notify_one();
  worker_.join();
}

void ControlDispatcher::run(){
  running_.store(true,std::memory_order_release);
  for(;;){
    const auto observed=mapper_.sequence_.load(std::memory_order_acquire);
    mapper_.dispatch_pending();
    if(stopping_.load(std::memory_order_acquire)){
      mapper_.dispatch_pending();
      break;
    }
    if(mapper_.sequence_.load(std::memory_order_acquire)==observed)
      mapper_.sequence_.wait(observed,std::memory_order_acquire);
  }
  running_.store(false,std::memory_order_release);
}
}
