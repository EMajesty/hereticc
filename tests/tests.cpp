#include "neurogenic/audio/engine.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include <array>
#include <cassert>
#include <iostream>
int main(){using namespace neurogenic;Scheduler s(1600,60);Pattern a;a.steps[0].enabled=true;a.steps[0].note=42;a.revision=1;s.request_pattern(a);std::array<ScheduledEvent,8> ev{};auto n=s.schedule(1601,ev);assert(n==1&&ev[0].frame==0&&ev[0].note==42);Pattern b;b.steps[4].enabled=true;b.revision=2;s.request_pattern(b);s.schedule(4799,ev);assert(s.active_pattern().revision==1);s.schedule(2,ev);assert(s.active_pattern().revision==2);bool mapped=false;MidiMapper m;m.map_button(0,12,"test",[&]{mapped=true;});assert(m.receive({0x90,12,1,0})&&mapped);std::string error;bool submitted=false;ScriptRuntime rt([&](Pattern p){submitted=p.steps[0].enabled;});assert(rt.execute("pattern x...............",error)&&submitted);assert(!rt.execute("bad",error));Sample sample{48000,1,std::vector<float>(16,1)};AudioEngine e;e.set_sample(sample);e.trigger(60,1);std::array<float,32> l{},r{};e.process(l,r);assert(l[0]>0&&l[20]==0);std::cout<<"all tests passed\n";}
