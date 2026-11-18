#pragma once
#include <string_view>
namespace neurogenic { enum class CommandId { transport_start, transport_stop, pattern_generate }; constexpr std::string_view stable_id(CommandId id){switch(id){case CommandId::transport_start:return"transport.start";case CommandId::transport_stop:return"transport.stop";case CommandId::pattern_generate:return"pattern.generate";}return"unknown";} }
