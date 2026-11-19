#pragma once
#include "neurogenic/application/control.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace neurogenic {
enum class GuiView:std::uint8_t{sequencer,samples,parameters,xy};
struct UiRect {float x{},y{},w{},h{};[[nodiscard]] bool contains(float px,float py) const noexcept{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
struct GuiLayout {float scale{1},offset_x{},offset_y{};UiRect top,nav,content,parameters,page_previous,page_next,resolution,target_note;std::vector<UiRect> tabs,grid,rows;};
enum class UiColor:std::uint8_t{background,panel,text,muted,accent,selected,error,unavailable};
struct DrawCommand {enum class Type:std::uint8_t{rectangle,text,line};Type type{};UiRect rect;UiColor color{};std::string text;};
struct GuiFrame {std::uint64_t state_revision{};int viewport_width{},viewport_height{};GuiLayout layout;std::vector<DrawCommand> commands;};
enum class GuiKey:std::uint8_t{left,right,up,down,enter,escape,tab,space,shift,number};
struct GuiInput {enum class Type:std::uint8_t{key_down,key_up,pointer_down,pointer_up,pointer_move,touch_down,touch_up,touch_move};Type type{};GuiKey key{};int number{};float x{},y{};std::uint64_t timestamp_ms{};};

class GuiModel {
public:
  explicit GuiModel(ApplicationService& application):application_(application){}
  [[nodiscard]] GuiLayout layout(int width,int height) const;
  [[nodiscard]] const GuiFrame& frame(int width,int height);
  bool input(const GuiInput&,int width,int height);
  [[nodiscard]] GuiView view() const noexcept{return view_;}
  [[nodiscard]] const std::string& error() const noexcept{return error_;}
  [[nodiscard]] std::vector<std::filesystem::directory_entry> browse(const std::filesystem::path&,std::string& error) const;
  void set_sample_path(std::filesystem::path path){sample_path_=std::move(path);}
private:
  void submit(ApplicationCommand);void render(const ApplicationState&,const GuiLayout&);
  ApplicationService& application_;GuiView view_{GuiView::sequencer};GuiFrame frame_;std::string error_;std::filesystem::path sample_path_;std::size_t parameter_index_{};bool pointer_down_{},shift_{};std::uint64_t last_xy_ms_{};
};

struct GuiOptions {bool fullscreen{};int width{1024},height{600};std::uint32_t max_frames{};};
[[nodiscard]] bool gui_available() noexcept;
int run_gui(ApplicationService&,const GuiOptions&,std::string& error);
}
