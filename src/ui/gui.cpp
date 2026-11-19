#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
ApplicationCommand command(std::string id,std::map<std::string,ApplicationValue> arguments={}){ApplicationCommand result;result.action_id=std::move(id);result.arguments=std::move(arguments);result.source_id="gui";return result;}
std::uint64_t step_count(const TrackSummary& track,MusicalTick resolution){return static_cast<std::uint64_t>((track.pattern_length+resolution-1)/resolution);}
const TrackSummary* selected_track(const ApplicationState& state){const auto found=std::find_if(state.tracks.begin(),state.tracks.end(),[&](const auto& track){return track.id==state.selected_track;});return found==state.tracks.end()?nullptr:&*found;}
StepResolution next_resolution(StepResolution value){return static_cast<StepResolution>((static_cast<unsigned>(value)+1)%8);}
}

GuiLayout GuiModel::layout(int width,int height) const{
  GuiLayout result;
  result.scale=std::max(0.5F,std::min(width/1024.0F,height/600.0F));
  const float logical_width=width/result.scale;
  const float logical_height=height/result.scale;
  result.offset_y=std::max(0.0F,(logical_height-600.0F)*0.5F)*result.scale;
  auto r=[&](float x,float y,float w,float h){return UiRect{x*result.scale,result.offset_y+y*result.scale,w*result.scale,h*result.scale};};
  result.top=r(0,0,logical_width,64);result.nav=r(0,64,logical_width,54);
  result.content=r(16,130,logical_width-280,454);result.parameters=r(logical_width-248,130,232,454);
  const float tab_width=(logical_width-48)/4;for(int i=0;i<4;++i)result.tabs.push_back(r(16+i*tab_width,72,tab_width-8,38));
  result.page_previous=r(28,142,54,34);result.page_next=r(88,142,54,34);result.resolution=r(150,142,92,34);result.target_note=r(250,142,92,34);
  for(int i=0;i<8;++i)result.rows.push_back(r(28,184+i*42,logical_width-316,34));
  const float grid_width=logical_width-328;const float cell=(grid_width-70)/8;
  for(int i=0;i<16;++i)result.grid.push_back(r(28+(i%8)*(cell+10),360+(i/8)*76,cell,66));
  return result;
}

const GuiFrame& GuiModel::frame(int width,int height){const auto state=application_.snapshot();if(frame_.state_revision!=state.revision||frame_.viewport_width!=width||frame_.viewport_height!=height){frame_.state_revision=state.revision;frame_.viewport_width=width;frame_.viewport_height=height;frame_.layout=layout(width,height);render(state,frame_.layout);}return frame_;}
void GuiModel::submit(ApplicationCommand value){auto result=application_.execute(value);error_=result.accepted?std::string{}:result.error;frame_.state_revision=std::numeric_limits<std::uint64_t>::max();}

void GuiModel::render(const ApplicationState& state,const GuiLayout& l){
  frame_.commands.clear();auto rect=[&](UiRect r,UiColor c){frame_.commands.push_back({DrawCommand::Type::rectangle,r,c,{}});};auto text=[&](UiRect r,UiColor c,std::string value){frame_.commands.push_back({DrawCommand::Type::text,r,c,std::move(value)});};
  rect({0,0,static_cast<float>(frame_.viewport_width),static_cast<float>(frame_.viewport_height)},UiColor::background);rect(l.top,UiColor::panel);
  text({l.top.x+16*l.scale,l.top.y+12*l.scale,l.top.w-32*l.scale,30*l.scale},UiColor::text,"NEUROGENIC   "+std::to_string(static_cast<int>(std::round(state.tempo_bpm)))+" BPM   "+std::to_string(state.time_signature_numerator)+"/"+std::to_string(state.time_signature_denominator)+"   "+(state.playing?"PLAY":"STOP")+"   TRACK "+std::to_string(state.selected_track)+"   "+pad_mode_name(state.pad_mode));
  const char* names[]{"SEQUENCER","SAMPLES","PARAMETERS","X/Y"};for(std::size_t i=0;i<l.tabs.size();++i){rect(l.tabs[i],i==static_cast<std::size_t>(view_)?UiColor::selected:UiColor::panel);text(l.tabs[i],UiColor::text,names[i]);}rect(l.content,UiColor::panel);rect(l.parameters,UiColor::panel);
  if(view_==GuiView::sequencer){
    const auto* track=selected_track(state);text({l.content.x+12*l.scale,l.content.y+8*l.scale,l.content.w-24*l.scale,28*l.scale},UiColor::accent,"TIMELINE / PAGE "+std::to_string(state.step_page+1));
    for(const auto control:{l.page_previous,l.page_next,l.resolution,l.target_note})rect(control,UiColor::background);
    text(l.page_previous,UiColor::text,"< PAGE");text(l.page_next,UiColor::text,"PAGE >");text(l.resolution,UiColor::text,step_resolution_name(state.step_resolution));text(l.target_note,UiColor::text,"NOTE "+std::to_string(track?track->target_note:0));
    for(std::size_t i=0;i<std::min<std::size_t>(3,state.tracks.size());++i){const auto& item=state.tracks[i];rect(l.rows[i],item.id==state.selected_track?UiColor::selected:UiColor::background);text(l.rows[i],item.muted?UiColor::muted:UiColor::text,std::to_string(item.id)+"  "+item.name+"  "+std::to_string(item.pattern_length)+" ticks"+(item.muted?"  MUTED":""));}
    const auto steps=track?step_count(*track,state.step_ticks):0;const auto playhead=track&&track->pattern_length>0?static_cast<std::uint64_t>((state.transport_tick%track->pattern_length)/state.step_ticks):0;
    for(std::size_t i=0;i<l.grid.size();++i){const auto step=state.step_page*16+i;const auto tick=static_cast<MusicalTick>(step)*state.step_ticks;const bool valid=step<steps;const bool active=valid&&std::any_of(track->events.begin(),track->events.end(),[&](const MusicalEvent& event){return event.type==MusicalEventType::note&&event.tick==tick&&event.pitch==track->target_note;});UiColor color=!valid?UiColor::unavailable:active?UiColor::accent:state.selected_step==static_cast<std::int64_t>(step)?UiColor::selected:UiColor::background;rect(l.grid[i],color);text(l.grid[i],step==playhead?UiColor::selected:UiColor::text,std::to_string(step+1)+(step==playhead?" >":""));}
  } else if(view_==GuiView::samples){text({l.content.x+12,l.content.y+8,l.content.w,28*l.scale},UiColor::accent,"SAMPLE SLOTS");for(std::size_t i=0;i<l.rows.size();++i){const auto slot=static_cast<std::uint32_t>(i);const auto found=std::find_if(state.sample_slots.begin(),state.sample_slots.end(),[&](const auto& s){return s.id.value==slot;});std::string label="SLOT "+std::to_string(slot)+"  EMPTY";if(found!=state.sample_slots.end())label="SLOT "+std::to_string(slot)+"  "+sample_slot_state_name(found->state)+"  "+found->source_path.filename().string()+(found->error.empty()?"":"  "+found->error);rect(l.rows[i],slot==state.selected_sample_slot.value?UiColor::selected:UiColor::background);text(l.rows[i],found!=state.sample_slots.end()&&!found->error.empty()?UiColor::error:UiColor::text,label);}}
  else if(view_==GuiView::parameters){text({l.content.x+12,l.content.y+8,l.content.w,28*l.scale},UiColor::accent,"APPLICATION PARAMETERS");for(std::size_t i=0;i<std::min(l.rows.size(),state.parameters.size());++i){const auto& p=state.parameters[i];rect(l.rows[i],i==parameter_index_?UiColor::selected:UiColor::background);std::string value=std::holds_alternative<double>(p.current_value)?std::to_string(std::get<double>(p.current_value)):"value";text(l.rows[i],UiColor::text,p.name+"  "+value+" "+p.unit);}}
  else{text({l.content.x+12,l.content.y+8,l.content.w,28*l.scale},UiColor::accent,"X/Y PERFORMANCE");UiRect pad{l.content.x+60*l.scale,l.content.y+56*l.scale,std::min(360*l.scale,l.content.w-120*l.scale),360*l.scale};rect(pad,UiColor::background);double x=.5,y=.5;for(const auto& p:state.parameters){if(p.id=="performance.xy.x")x=std::get<double>(p.current_value);if(p.id=="performance.xy.y")y=std::get<double>(p.current_value);}rect({pad.x+static_cast<float>(x)*pad.w-10*l.scale,pad.y+static_cast<float>(y)*pad.h-10*l.scale,20*l.scale,20*l.scale},UiColor::accent);}
  text({l.parameters.x+12,l.parameters.y+12,l.parameters.w-24,28*l.scale},UiColor::accent,"CONTEXT");text({l.parameters.x+12,l.parameters.y+54*l.scale,l.parameters.w-24,130*l.scale},UiColor::text,"Focus: "+state.focused_element+"\nStep: "+std::to_string(state.selected_step+1)+"\nResolution: "+step_resolution_name(state.step_resolution)+"\nRevision: "+std::to_string(state.revision));if(!error_.empty())text({l.parameters.x+12,l.parameters.y+l.parameters.h-70*l.scale,l.parameters.w-24,60*l.scale},UiColor::error,error_);
}

bool GuiModel::input(const GuiInput& input,int width,int height){
  const auto l=layout(width,height);const auto state=application_.snapshot();const auto* track=selected_track(state);auto simple=[&](std::string id){submit(command(std::move(id)));};
  if(input.type==GuiInput::Type::key_down){
    if(input.key==GuiKey::shift){shift_=true;submit(command("modifier.shift.v1",{{"active",true}}));return true;}
    if(input.key==GuiKey::space){simple(state.playing?"transport.stop.v1":"transport.play.v1");return true;}
    if(view_==GuiView::sequencer){
      if(input.key==GuiKey::enter){simple("step.toggle.v1");return true;}
      if(input.key==GuiKey::left||input.key==GuiKey::right){if(shift_)simple(input.key==GuiKey::right?"step.page-next.v1":"step.page-previous.v1");else{const auto steps=track?step_count(*track,state.step_ticks):0;if(steps){auto step=state.selected_step+(input.key==GuiKey::right?1:-1);if(step<0)step=static_cast<std::int64_t>(steps-1);if(step>=static_cast<std::int64_t>(steps))step=0;submit(command("step.select.v1",{{"step",step}}));}}return true;}
      if(input.key==GuiKey::up||input.key==GuiKey::down){if(shift_&&track){const auto note=std::clamp<int>(track->target_note+(input.key==GuiKey::up?1:-1),0,127);submit(command("track.note-target-set.v1",{{"note",static_cast<std::int64_t>(note)}}));}else simple(input.key==GuiKey::down?"track.next.v1":"track.previous.v1");return true;}
      if(input.key==GuiKey::number&&input.number>=1&&input.number<=16){const auto step=static_cast<std::int64_t>(state.step_page*16+input.number-1);submit(command(shift_?"step.select.v1":"step.toggle.v1",{{"step",step}}));return true;}
    }
    if(input.key==GuiKey::tab){view_=static_cast<GuiView>((static_cast<unsigned>(view_)+1)%4);frame_.state_revision=std::numeric_limits<std::uint64_t>::max();return true;}
    if(input.key==GuiKey::enter){if(view_==GuiView::samples&&!sample_path_.empty())submit(command("sample.replace.v1",{{"slot_id",static_cast<std::int64_t>(state.selected_sample_slot.value)},{"path",sample_path_.string()}}));else simple("navigation.confirm.v1");return true;}
    if(input.key==GuiKey::escape){simple("navigation.cancel.v1");return true;}
    if((input.key==GuiKey::up||input.key==GuiKey::down)&&view_==GuiView::parameters&&!state.parameters.empty()){submit(command("parameter.adjust.v1",{{"parameter_id",state.parameters[parameter_index_%state.parameters.size()].id},{"steps",input.key==GuiKey::up?1.0:-1.0}}));return true;}
  }
  if(input.type==GuiInput::Type::key_up&&input.key==GuiKey::shift){shift_=false;submit(command("modifier.shift.v1",{{"active",false}}));return true;}
  const bool down=input.type==GuiInput::Type::pointer_down||input.type==GuiInput::Type::touch_down;const bool move=input.type==GuiInput::Type::pointer_move||input.type==GuiInput::Type::touch_move;const bool up=input.type==GuiInput::Type::pointer_up||input.type==GuiInput::Type::touch_up;
  if(down){pointer_down_=true;for(std::size_t i=0;i<l.tabs.size();++i)if(l.tabs[i].contains(input.x,input.y)){view_=static_cast<GuiView>(i);frame_.state_revision=std::numeric_limits<std::uint64_t>::max();return true;}if(view_==GuiView::sequencer){if(l.page_previous.contains(input.x,input.y)){simple("step.page-previous.v1");return true;}if(l.page_next.contains(input.x,input.y)){simple("step.page-next.v1");return true;}if(l.resolution.contains(input.x,input.y)){submit(command("step.resolution-set.v1",{{"resolution",std::string(step_resolution_name(next_resolution(state.step_resolution)))}}));return true;}if(l.target_note.contains(input.x,input.y)&&track){submit(command("track.note-target-set.v1",{{"note",static_cast<std::int64_t>(std::min<int>(127,track->target_note+1))}}));return true;}for(std::size_t i=0;i<std::min<std::size_t>(3,state.tracks.size());++i)if(l.rows[i].contains(input.x,input.y)){submit(command("track.select.v1",{{"track_id",static_cast<std::int64_t>(state.tracks[i].id)}}));return true;}for(std::size_t i=0;i<l.grid.size();++i)if(l.grid[i].contains(input.x,input.y)){submit(command("step.toggle.v1",{{"step",static_cast<std::int64_t>(state.step_page*16+i)}}));return true;}}if(view_==GuiView::samples)for(std::size_t i=0;i<l.rows.size();++i)if(l.rows[i].contains(input.x,input.y)){submit(command("sample.slot-select.v1",{{"slot_id",static_cast<std::int64_t>(i)}}));return true;}if(view_==GuiView::parameters)for(std::size_t i=0;i<std::min(l.rows.size(),state.parameters.size());++i)if(l.rows[i].contains(input.x,input.y)){parameter_index_=i;frame_.state_revision=std::numeric_limits<std::uint64_t>::max();return true;}}
  if(((move&&pointer_down_)||down)&&view_==GuiView::xy){UiRect pad{l.content.x+60*l.scale,l.content.y+56*l.scale,std::min(360*l.scale,l.content.w-120*l.scale),360*l.scale};if(pad.contains(input.x,input.y)&&(input.timestamp_ms>=last_xy_ms_+8||down)){last_xy_ms_=input.timestamp_ms;submit(command("modulation.xy.v1",{{"position",NormalizedXY{std::clamp((input.x-pad.x)/pad.w,0.0F,1.0F),std::clamp((input.y-pad.y)/pad.h,0.0F,1.0F)}}}));return true;}}
  if(up){pointer_down_=false;return true;}return false;
}

std::vector<std::filesystem::directory_entry> GuiModel::browse(const std::filesystem::path& path,std::string& error) const{try{std::vector<std::filesystem::directory_entry> result;for(const auto& entry:std::filesystem::directory_iterator(path))if(entry.is_directory()||entry.path().extension()==".wav")result.push_back(entry);std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.path()<b.path();});error.clear();return result;}catch(const std::exception& exception){error=exception.what();return{};}}
}
