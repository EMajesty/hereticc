cmake_minimum_required(VERSION 3.22)
project(neurogenic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(NEUROGENIC_FETCH_DEPENDENCIES "Fetch pinned audio, MIDI, Lua and sol2 dependencies" OFF)
option(NEUROGENIC_BUILD_TESTS "Build tests" ON)
option(NEUROGENIC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(NEUROGENIC_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "NEUROGENIC_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
endif()

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

add_library(neurogenic_engine
  src/audio/engine.cpp src/audio/sample.cpp src/core/command.cpp src/core/control_dispatcher.cpp
  src/dsp/purest_gain.cpp src/midi/mapping.cpp src/project/project.cpp
  src/sequencer/scheduler.cpp src/scripting/runtime.cpp)
target_include_directories(neurogenic_engine PUBLIC include)
target_compile_options(neurogenic_engine PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(neurogenic_engine PUBLIC Threads::Threads)

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
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Retain ALSA as an ELF dependency so Nix records alsa-lib in RUNPATH.
    target_compile_definitions(neurogenic_live PRIVATE NEUROGENIC_HAS_ALSA)
    target_link_libraries(neurogenic_live PUBLIC ALSA::ALSA)
  endif()
endif()

add_executable(neurogenic apps/neurogenic.cpp)
target_link_libraries(neurogenic PRIVATE neurogenic_engine neurogenic_live)
install(TARGETS neurogenic RUNTIME DESTINATION bin)
install(FILES examples/generate.lua DESTINATION share/neurogenic/examples)

if(NEUROGENIC_BUILD_TESTS)
  enable_testing()
  add_executable(neurogenic_tests tests/tests.cpp)
  target_link_libraries(neurogenic_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_tests COMMAND neurogenic_tests)
  if(NEUROGENIC_FETCH_DEPENDENCIES AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME neurogenic_alsa_runtime_link COMMAND ${CMAKE_COMMAND}
      -DNEUROGENIC_EXECUTABLE=$<TARGET_FILE:neurogenic>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAlsaRuntime.cmake)
  endif()
endif()
