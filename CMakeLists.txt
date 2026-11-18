cmake_minimum_required(VERSION 3.22)
project(neurogenic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(NEUROGENIC_FETCH_DEPENDENCIES "Fetch pinned audio, MIDI, Lua and sol2 dependencies" OFF)
option(NEUROGENIC_BUILD_TESTS "Build tests" ON)

include(cmake/Dependencies.cmake)

add_library(neurogenic_engine
  src/audio/engine.cpp src/audio/sample.cpp src/core/command.cpp
  src/dsp/purest_gain.cpp src/midi/mapping.cpp src/project/project.cpp
  src/sequencer/scheduler.cpp src/scripting/runtime.cpp)
target_include_directories(neurogenic_engine PUBLIC include)
target_compile_options(neurogenic_engine PRIVATE -Wall -Wextra -Wpedantic)

if(NEUROGENIC_FETCH_DEPENDENCIES)
  target_compile_definitions(neurogenic_engine PRIVATE NEUROGENIC_HAS_MINIAUDIO PUBLIC NEUROGENIC_HAS_LUA)
  target_link_libraries(neurogenic_engine PRIVATE neurogenic_miniaudio sol2::sol2 lua_static)
endif()

add_library(neurogenic_live src/audio/device.cpp src/midi/input.cpp)
target_include_directories(neurogenic_live PUBLIC include)
target_link_libraries(neurogenic_live PUBLIC neurogenic_engine)
if(NEUROGENIC_FETCH_DEPENDENCIES)
  target_compile_definitions(neurogenic_live PRIVATE NEUROGENIC_HAS_MINIAUDIO NEUROGENIC_HAS_LIBREMIDI)
  target_link_libraries(neurogenic_live PRIVATE neurogenic_miniaudio libremidi)
endif()

add_executable(neurogenic apps/neurogenic.cpp)
target_link_libraries(neurogenic PRIVATE neurogenic_engine neurogenic_live)

if(NEUROGENIC_BUILD_TESTS)
  enable_testing()
  add_executable(neurogenic_tests tests/tests.cpp)
  target_link_libraries(neurogenic_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_tests COMMAND neurogenic_tests)
endif()
