#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/gui.hpp"
#include <iostream>

int main(){
  using namespace neurogenic;
  if(!gui_available()){std::cout<<"SDL3 GUI not built; smoke test skipped\n";return 0;}
  AudioEngine engine;
  TimelineService timeline([&](const Timeline& value,std::string& error){return engine.queue_timeline(value,error);});
  SampleManager samples(engine);
  ApplicationService application(engine,timeline,samples);
  std::string error;
  GuiOptions options;options.max_frames=2;
  const int result=run_gui(application,options,error);
  samples.shutdown();
  if(result)std::cerr<<error<<'\n';
  return result;
}
