#include "neurogenic/scripting/runtime.hpp"
#ifdef NEUROGENIC_HAS_LUA
#include <sol/sol.hpp>
#endif
namespace neurogenic {
struct ScriptRuntime::Impl {
  Submit submit;Position position;
#ifdef NEUROGENIC_HAS_LUA
  sol::state lua;
#endif
  Impl(Submit s,Position p):submit(std::move(s)),position(std::move(p)){}
};
ScriptRuntime::ScriptRuntime(Submit submit,Position position):impl_(std::make_unique<Impl>(std::move(submit),std::move(position))){
#ifdef NEUROGENIC_HAS_LUA
  struct LuaPattern{Pattern value;void set_step(int index,bool enabled,int note,double velocity){if(index<1||index>16)throw std::runtime_error("step index must be 1..16");if(note<0||note>127)throw std::runtime_error("note must be 0..127");if(velocity<0||velocity>1)throw std::runtime_error("velocity must be 0..1");auto& s=value.steps[static_cast<std::size_t>(index-1)];s.enabled=enabled;s.note=static_cast<std::uint8_t>(note);s.velocity=static_cast<float>(velocity);}};
  impl_->lua.open_libraries(sol::lib::base,sol::lib::math,sol::lib::table,sol::lib::string);
  impl_->lua.new_usertype<LuaPattern>("NeurogenicPattern","set_step",&LuaPattern::set_step);
  auto ng=impl_->lua.create_named_table("neurogenic");auto v1=impl_->lua.create_table();ng["v1"]=v1;auto pattern=impl_->lua.create_table();v1["pattern"]=pattern;
  pattern.set_function("new",[]{return LuaPattern{};});
  pattern.set_function("queue",[this](LuaPattern& p){if(!valid_pattern(p.value)||!impl_->submit(p.value))throw std::runtime_error("pattern queue full or invalid");});
  auto transport=impl_->lua.create_table();v1["transport"]=transport;transport.set_function("frame",[this]{return impl_->position();});
#endif
}
ScriptRuntime::~ScriptRuntime()=default;
bool ScriptRuntime::execute(const std::string& source,std::string& error) noexcept {
#ifdef NEUROGENIC_HAS_LUA
  auto result=impl_->lua.safe_script(source,sol::script_pass_on_error);if(!result.valid()){sol::error e=result;error=e.what();return false;}error.clear();return true;
#else
  (void)source;error="Lua support was not built";return false;
#endif
}
bool ScriptRuntime::execute_file(const std::filesystem::path& path,std::string& error) noexcept {
#ifdef NEUROGENIC_HAS_LUA
  auto result=impl_->lua.safe_script_file(path.string(),sol::script_pass_on_error);if(!result.valid()){sol::error e=result;error=e.what();return false;}error.clear();return true;
#else
  (void)path;error="Lua support was not built";return false;
#endif
}
}
