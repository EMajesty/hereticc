#include "neurogenic/audio/sample_manager.hpp"
#include <exception>
#include <stdexcept>

namespace neurogenic {
const char* sample_slot_state_name(SampleSlotLoadState state) noexcept{switch(state){case SampleSlotLoadState::empty:return"empty";case SampleSlotLoadState::loading:return"loading";case SampleSlotLoadState::loaded:return"loaded";case SampleSlotLoadState::error:return"error";}return"unknown";}

SampleManager::SampleManager(AudioEngine& engine,std::size_t command_capacity):engine_(engine),capacity_(command_capacity){if(capacity_==0)throw std::invalid_argument("sample command capacity must be positive");worker_=std::thread([this]{run();});}
SampleManager::~SampleManager(){shutdown();}

bool SampleManager::submit(Command command){std::lock_guard lock(mutex_);if(stopping_||commands_.size()>=capacity_){++rejected_;return false;}command.revision=next_revision_++;auto& state=states_[command.id.value];state.id=command.id;state.state=SampleSlotLoadState::loading;state.engine_sample_rate=engine_.sample_rate();state.progress=0;state.error.clear();state.revision=command.revision;commands_.push_back(std::move(command));wake_.notify_one();return true;}
bool SampleManager::load(SampleSlotId id,const std::filesystem::path& path){if(id==invalid_sample_slot||path.empty())return false;return submit({Operation::load,id,path,{}});}
bool SampleManager::unload(SampleSlotId id){if(id==invalid_sample_slot)return false;return submit({Operation::unload,id,{},{}});}

SampleSlotStatus SampleManager::status(SampleSlotId id) const{std::lock_guard lock(mutex_);const auto found=states_.find(id.value);if(found!=states_.end())return found->second;SampleSlotStatus empty;empty.id=id;empty.engine_sample_rate=engine_.sample_rate();return empty;}
std::vector<SampleSlotStatus> SampleManager::states() const{std::lock_guard lock(mutex_);std::vector<SampleSlotStatus> result;result.reserve(states_.size());for(const auto& [id,state]:states_){(void)id;result.push_back(state);}return result;}
std::uint64_t SampleManager::rejected_commands() const noexcept{std::lock_guard lock(mutex_);return rejected_;}
void SampleManager::wait_idle(){std::unique_lock lock(mutex_);idle_.wait(lock,[this]{return commands_.empty()&&!busy_;});engine_.reclaim_sample_versions();}
void SampleManager::shutdown() noexcept{{std::lock_guard lock(mutex_);if(stopping_)return;stopping_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();engine_.reclaim_sample_versions();}

void SampleManager::run() noexcept{WindowedSincConverter converter;for(;;){Command command;{
      std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||!commands_.empty();});if(commands_.empty()){if(stopping_)break;continue;}command=std::move(commands_.front());commands_.pop_front();busy_=true;}
    if(command.operation==Operation::unload){const auto accepted=engine_.unload_sample_slot(command.id);std::lock_guard lock(mutex_);auto& state=states_[command.id.value];if(accepted){state.has_sample=false;state.source_path.clear();state.source_sample_rate=0;state.channels=0;state.frames=0;state.duration_seconds=0;}if(state.revision==command.revision){if(accepted){state.state=SampleSlotLoadState::empty;state.progress=1;state.error.clear();}else{state.state=SampleSlotLoadState::error;state.error="real-time sample update queue is full";}}}
    else {try{auto decoded=decode_wav(command.path);const auto source_rate=decoded.sample_rate;const auto channels=decoded.channels;auto prepared=prepare_sample(std::move(decoded),engine_.sample_rate(),converter);const auto frames=prepared.frames();if(!engine_.publish_sample_slot(command.id,std::move(prepared)))throw std::runtime_error("sample publication capacity is exhausted");std::lock_guard lock(mutex_);auto& state=states_[command.id.value];state.has_sample=true;state.source_path=command.path;state.source_sample_rate=source_rate;state.engine_sample_rate=engine_.sample_rate();state.channels=channels;state.frames=frames;state.duration_seconds=static_cast<double>(frames)/engine_.sample_rate();if(state.revision==command.revision){state.state=SampleSlotLoadState::loaded;state.progress=1;state.error.clear();}}
      catch(const std::exception& error){std::lock_guard lock(mutex_);auto& state=states_[command.id.value];if(state.revision==command.revision){state.state=SampleSlotLoadState::error;state.progress=1;state.error=error.what();}}}
    {std::lock_guard lock(mutex_);busy_=false;if(commands_.empty())idle_.notify_all();}
  }std::lock_guard lock(mutex_);busy_=false;idle_.notify_all();}
}
