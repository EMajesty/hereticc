#include "neurogenic/project/project.hpp"
#include <fstream>
#include <stdexcept>
namespace neurogenic { void save_project(const Project&p,const std::filesystem::path&f){std::ofstream o(f);o<<"NEUROGENIC 1\n";for(auto&s:p.pattern.steps)o<<(s.enabled?'x':'.');o<<'\n';}Project load_project(const std::filesystem::path&f){std::ifstream i(f);std::string magic,mask;unsigned v;i>>magic>>v>>mask;if(magic!="NEUROGENIC"||v!=1||mask.size()!=16)throw std::runtime_error("unsupported project");Project p;for(std::size_t n=0;n<16;++n)p.pattern.steps[n].enabled=mask[n]=='x';return p;} }
