#include "neurogenic/audio/sample_manager.hpp"
#include <exception>
#include <stdexcept>

namespace neurogenic {
const char* sample_slot_state_name(SampleSlotLoadState state) noexcept{switch(state){case SampleSlotLoadState::empty:return"empty";case SampleSlotLoadState::queued:return"queued";case SampleSlotLoadState::preparing:return"preparing";case SampleSlotLoadState::prepared:return"prepared";case SampleSlotLoadState::pending_activation:return"pending-activation";case SampleSlotLoadState::loaded:return"loaded";case SampleSlotLoadState::error:return"error";}return"unknown";}

SampleManager::SampleManager(AudioEngine& engine,std::size_t command_capacity):engine_(engine),capacity_(command_capacity){if(capacity_==0)throw std::invalid_argument("sample command capacity must be positive");worker_=std::thread([this]{run();});}
SampleManager::~SampleManager(){shutdown();}

bool SampleManager::submit(Command command){std::lock_guard lock(mutex_);if(stopping_||commands_.size()>=capacity_){++rejected_;return false;}command.revision=next_revision_++;auto& record=states_[command.id.value];auto& state=record.status;state.id=command.id;state.state=SampleSlotLoadState::queued;state.engine_sample_rate=engine_.sample_rate();state.progress=0;state.error.clear();state.revision=command.revision;state.pending_source_path=command.path;commands_.push_back(std::move(command));wake_.notify_one();return true;}
bool SampleManager::load(SampleSlotId id,const std::filesystem::path& path){if(id==invalid_sample_slot||path.empty())return false;return submit({Operation::load,id,path,{}});}
bool SampleManager::unload(SampleSlotId id){if(id==invalid_sample_slot)return false;return submit({Operation::unload,id,{},{}});}

void SampleManager::refresh_locked(SlotRecord& record) const{
  while(!record.publications.empty()&&engine_.sample_update_applied(record.publications.front().sequence)){
    auto publication=std::move(record.publications.front());record.publications.pop_front();auto& state=record.status;
    if(publication.operation==Operation::unload){state.has_sample=false;state.source_path.clear();state.source_sample_rate=0;state.channels=0;state.frames=0;state.duration_seconds=0;}
    else {state.has_sample=true;state.source_path=std::move(publication.path);state.source_sample_rate=publication.source_rate;state.engine_sample_rate=engine_.sample_rate();state.channels=publication.channels;state.frames=publication.frames;state.duration_seconds=publication.duration_seconds;}
    if(state.revision==publication.revision){state.state=publication.operation==Operation::unload?SampleSlotLoadState::empty:SampleSlotLoadState::loaded;state.progress=1;state.error.clear();state.pending_source_path.clear();}
  }
}

SampleSlotStatus SampleManager::status(SampleSlotId id) const{std::lock_guard lock(mutex_);const auto found=states_.find(id.value);if(found!=states_.end()){refresh_locked(found->second);return found->second.status;}SampleSlotStatus empty;empty.id=id;empty.engine_sample_rate=engine_.sample_rate();return empty;}
std::vector<SampleSlotStatus> SampleManager::states() const{std::lock_guard lock(mutex_);std::vector<SampleSlotStatus> result;result.reserve(states_.size());for(auto& [id,record]:states_){(void)id;refresh_locked(record);result.push_back(record.status);}return result;}
std::uint64_t SampleManager::rejected_commands() const noexcept{std::lock_guard lock(mutex_);return rejected_;}
void SampleManager::wait_idle(){std::unique_lock lock(mutex_);idle_.wait(lock,[this]{return commands_.empty()&&!busy_;});lock.unlock();engine_.reclaim_sample_versions();}
void SampleManager::shutdown() noexcept{{std::lock_guard lock(mutex_);if(stopping_)return;stopping_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();engine_.reclaim_sample_versions();}

void SampleManager::run() noexcept{WindowedSincConverter converter;for(;;){Command command;{
      std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||!commands_.empty();});if(commands_.empty()){if(stopping_)break;continue;}command=std::move(commands_.front());commands_.pop_front();busy_=true;auto& state=states_[command.id.value].status;if(state.revision==command.revision)state.state=SampleSlotLoadState::preparing;}
    if(command.operation==Operation::unload){std::uint64_t publication{};const auto accepted=engine_.unload_sample_slot(command.id,&publication);std::lock_guard lock(mutex_);auto& record=states_[command.id.value];if(accepted){Publication pending;pending.operation=Operation::unload;pending.revision=command.revision;pending.sequence=publication;record.publications.push_back(std::move(pending));}if(record.status.revision==command.revision){if(accepted){record.status.state=SampleSlotLoadState::pending_activation;record.status.progress=1;}else{record.status.state=SampleSlotLoadState::error;record.status.progress=1;record.status.error="real-time sample update queue is full";record.status.pending_source_path.clear();}}}
    else {try{auto decoded=decode_wav(command.path);const auto source_rate=decoded.sample_rate;const auto channels=decoded.channels;auto prepared=prepare_sample(std::move(decoded),engine_.sample_rate(),converter);const auto frames=prepared.frames();{std::lock_guard lock(mutex_);auto& state=states_[command.id.value].status;if(state.revision==command.revision){state.state=SampleSlotLoadState::prepared;state.progress=1;}}std::uint64_t publication{};if(!engine_.publish_sample_slot(command.id,std::move(prepared),&publication))throw std::runtime_error("sample publication capacity is exhausted");std::lock_guard lock(mutex_);auto& record=states_[command.id.value];record.publications.push_back({Operation::load,command.revision,publication,command.path,source_rate,channels,frames,static_cast<double>(frames)/engine_.sample_rate()});if(record.status.revision==command.revision)record.status.state=SampleSlotLoadState::pending_activation;}
      catch(const std::exception& error){std::lock_guard lock(mutex_);auto& state=states_[command.id.value].status;if(state.revision==command.revision){state.state=SampleSlotLoadState::error;state.progress=1;state.error=error.what();state.pending_source_path.clear();}}}
    {std::lock_guard lock(mutex_);busy_=false;if(commands_.empty())idle_.notify_all();}
  }std::lock_guard lock(mutex_);busy_=false;idle_.notify_all();}
}
