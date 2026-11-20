cmake_minimum_required(VERSION 3.22)
project(neurogenic VERSION 0.1.0 LANGUAGES C CXX)

set(NEUROGENIC_BUILD_COMMIT "" CACHE STRING "Source commit recorded in diagnostics")
if(NOT NEUROGENIC_BUILD_COMMIT)
  execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE NEUROGENIC_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
if(NOT NEUROGENIC_BUILD_COMMIT)
  set(NEUROGENIC_BUILD_COMMIT "unknown")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(NEUROGENIC_FETCH_DEPENDENCIES "Fetch pinned audio, MIDI, Lua and sol2 dependencies" OFF)
option(NEUROGENIC_BUILD_TESTS "Build tests" ON)
option(NEUROGENIC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NEUROGENIC_ENABLE_GUI "Build the optional SDL3 graphical frontend" ON)

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
  src/application/control.cpp src/application/controller.cpp src/application/controller_protocol.cpp src/application/lifecycle.cpp src/application/performance_macros.cpp
  src/audio/engine.cpp src/audio/mixer.cpp src/audio/recording.cpp src/audio/sample.cpp src/audio/sample_manager.cpp src/audio/slicing.cpp src/core/command.cpp src/core/control_dispatcher.cpp
  src/dsp/effect_chain.cpp src/dsp/modulation.cpp src/dsp/voice_modulation.cpp src/dsp/purest_gain.cpp src/dsp/processor.cpp src/dsp/parameter_destination.cpp src/dsp/time_stretch.cpp src/instrument/subtractive_synth.cpp src/midi/mapping.cpp src/midi/routing.cpp src/project/project.cpp
  src/diagnostics/hardware.cpp
  src/performance/monitor.cpp
  src/sequencer/scheduler.cpp src/sequencer/pattern_adapter.cpp src/scripting/runtime.cpp src/timeline/arrangement.cpp src/timeline/arrangement_recording.cpp src/timeline/edit.cpp src/timeline/io.cpp src/timeline/playback.cpp)
target_include_directories(neurogenic_engine PUBLIC include)
target_compile_options(neurogenic_engine PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(neurogenic_engine PRIVATE
  NEUROGENIC_VERSION="${PROJECT_VERSION}"
  NEUROGENIC_COMMIT="${NEUROGENIC_BUILD_COMMIT}")
target_link_libraries(neurogenic_engine PUBLIC Threads::Threads)

add_library(neurogenic_gui src/ui/gui.cpp src/ui/sdl_backend.cpp)
target_include_directories(neurogenic_gui PUBLIC include)
target_link_libraries(neurogenic_gui PUBLIC neurogenic_engine)
target_compile_options(neurogenic_gui PRIVATE -Wall -Wextra -Wpedantic)
if(NEUROGENIC_ENABLE_GUI)
  find_package(SDL3 QUIET CONFIG)
  if(SDL3_FOUND)
    target_compile_definitions(neurogenic_gui PRIVATE NEUROGENIC_HAS_SDL3)
    target_link_libraries(neurogenic_gui PRIVATE SDL3::SDL3)
    message(STATUS "NEUROGENIC: SDL3 graphical frontend enabled")
  else()
    message(STATUS "NEUROGENIC: SDL3 not found; building GUI-unavailable stub")
  endif()
endif()

if(NEUROGENIC_FETCH_DEPENDENCIES)
  target_compile_definitions(neurogenic_engine PRIVATE NEUROGENIC_HAS_MINIAUDIO PUBLIC NEUROGENIC_HAS_LUA)
  target_link_libraries(neurogenic_engine PRIVATE neurogenic_miniaudio sol2::sol2 lua_static)
endif()

add_library(neurogenic_live src/audio/device.cpp src/midi/input.cpp src/midi/output.cpp)
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
target_link_libraries(neurogenic PRIVATE neurogenic_engine neurogenic_live neurogenic_gui)
add_executable(neurogenic_diagnostics apps/neurogenic_diagnostics.cpp)
target_link_libraries(neurogenic_diagnostics PRIVATE neurogenic_engine neurogenic_live neurogenic_gui)
set_target_properties(neurogenic_diagnostics PROPERTIES OUTPUT_NAME neurogenic-diagnostics)
install(TARGETS neurogenic neurogenic_diagnostics RUNTIME DESTINATION bin)
install(FILES examples/generate.lua examples/timeline_drums.lua examples/timeline_mutate.lua
  examples/timeline_polyrhythm.lua examples/timeline_variation.lua DESTINATION share/neurogenic/examples)
install(DIRECTORY examples/controllers DESTINATION share/neurogenic/examples)
install(PROGRAMS packaging/neurogenic-appliance DESTINATION share/neurogenic/appliance)
install(FILES packaging/neurogenic-appliance.service DESTINATION share/neurogenic/appliance)

if(NEUROGENIC_BUILD_TESTS)
  enable_testing()
  add_executable(neurogenic_tests tests/tests.cpp)
  target_link_libraries(neurogenic_tests PRIVATE neurogenic_engine neurogenic_gui)
  target_compile_definitions(neurogenic_tests PRIVATE NEUROGENIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME neurogenic_tests COMMAND neurogenic_tests)
  add_executable(neurogenic_gui_smoke tests/gui_smoke.cpp)
  target_link_libraries(neurogenic_gui_smoke PRIVATE neurogenic_engine neurogenic_gui)
  add_test(NAME neurogenic_gui_smoke COMMAND neurogenic_gui_smoke)
  set_tests_properties(neurogenic_gui_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_schema
    COMMAND neurogenic_diagnostics --headless --json)
  set_tests_properties(neurogenic_diagnostics_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.hardware-diagnostics.v1" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_stress
    COMMAND neurogenic_diagnostics --headless --stress 0.05 --buffer 127
      --sample-voices 8 --synth-voices 4)
  set_tests_properties(neurogenic_diagnostics_stress PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.hardware-test-report.v1" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_device_lifecycle
    COMMAND neurogenic_diagnostics --headless --realtime --stress 0.05
      --output-channels 2)
  set_tests_properties(neurogenic_diagnostics_device_lifecycle PROPERTIES
    PASS_REGULAR_EXPRESSION "realtime-device-stress" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_display_diagnostics
    COMMAND neurogenic_diagnostics --display-test)
  set_tests_properties(neurogenic_display_diagnostics PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.display-test.v1" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_invalid_configuration
    COMMAND neurogenic_diagnostics --stress 1 --buffer 0)
  set_tests_properties(neurogenic_diagnostics_invalid_configuration PROPERTIES
    WILL_FAIL TRUE TIMEOUT 10)
  add_test(NAME neurogenic_appliance_lifecycle_schema
    COMMAND neurogenic_diagnostics --lifecycle-test)
  set_tests_properties(neurogenic_appliance_lifecycle_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.appliance-lifecycle.v1" TIMEOUT 10)
  add_test(NAME neurogenic_controller_protocol_schema
    COMMAND neurogenic_diagnostics --controller-protocol-test)
  set_tests_properties(neurogenic_controller_protocol_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.controller-protocol.v1" TIMEOUT 10)
  add_test(NAME neurogenic_appliance_start_stop
    COMMAND neurogenic --headless --gui --run-for 0.05 --preferences
      ${CMAKE_CURRENT_BINARY_DIR}/appliance-smoke.conf)
  set_tests_properties(neurogenic_appliance_start_stop PROPERTIES
    PASS_REGULAR_EXPRESSION "Stopped. dropped MIDI=" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_appliance_missing_hardware
    COMMAND neurogenic --headless --gui --audio definitely-missing-audio
      --midi definitely-missing-midi --run-for 0.05 --preferences
      ${CMAKE_CURRENT_BINARY_DIR}/appliance-missing-hardware.conf)
  set_tests_properties(neurogenic_appliance_missing_hardware PROPERTIES
    PASS_REGULAR_EXPRESSION "\"degraded\":true" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_appliance_missing_controller
    COMMAND neurogenic --headless --gui --controller
      /dev/neurogenic-definitely-missing-controller --run-for 0.05
      --preferences ${CMAKE_CURRENT_BINARY_DIR}/appliance-missing-controller.conf)
  set_tests_properties(neurogenic_appliance_missing_controller PROPERTIES
    PASS_REGULAR_EXPRESSION "controller unavailable.*will retry exact path"
    TIMEOUT 10 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_executable(neurogenic_workflows tests/workflow_scenarios.cpp)
  target_link_libraries(neurogenic_workflows PRIVATE neurogenic_engine neurogenic_gui)
  add_test(NAME neurogenic_workflows COMMAND neurogenic_workflows all)
  set_tests_properties(neurogenic_workflows PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.workflow.summary.v1 passed=4"
    TIMEOUT 30)
  add_executable(neurogenic_benchmark benchmarks/timeline_benchmark.cpp)
  target_link_libraries(neurogenic_benchmark PRIVATE neurogenic_engine neurogenic_gui)
  # CTest validates every scenario and runs it under sanitizers, but does not
  # collect performance data. Keep this bounded independently of runner speed;
  # the benchmark executable retains its 2,000-iteration measurement default.
  add_test(NAME neurogenic_benchmark_schema COMMAND neurogenic_benchmark --iterations 100)
  set_tests_properties(neurogenic_benchmark_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.benchmark.summary.v1" TIMEOUT 30)
  if(NEUROGENIC_FETCH_DEPENDENCIES AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME neurogenic_alsa_runtime_link COMMAND ${CMAKE_COMMAND}
      -DNEUROGENIC_EXECUTABLE=$<TARGET_FILE:neurogenic>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAlsaRuntime.cmake)
  endif()
endif()
