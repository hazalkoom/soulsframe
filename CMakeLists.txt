cmake_minimum_required(VERSION 3.25)
project(soulsframe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SF_PROBE "Build RE/telemetry features" OFF)
option(SF_HOST_ONLY "Build only the native pure-C++ parts (tests/tools)" OFF)
option(SF_SANITIZE "Enable Address and UndefinedBehavior sanitizers on host builds" OFF)
option(SF_COVERAGE "Enable gcov/lcov code coverage instrumentation on host builds" OFF)
option(SF_BUILD_TESTBED "Build the D3D11 smoke-testbed executable" OFF)
option(SF_WERROR "Treat compiler warnings as errors on SoulsFrame targets" OFF)

if(SF_WERROR)
  set(SF_WARN_FLAGS -Wall -Wextra -Wpedantic -Werror)
else()
  set(SF_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()

include(cmake/deps.cmake)

add_library(soulsframe_core STATIC
  src/core/config.cpp
  src/core/module_registry.cpp
  src/core/offline_guard.cpp
  src/core/sha256.cpp
  src/games/ds3/profile.cpp
  src/hooks/sigscan.cpp
  src/tex/content_hash.cpp
  src/timing/frame_stats.cpp
)
target_include_directories(soulsframe_core PUBLIC src)
target_compile_options(soulsframe_core PRIVATE ${SF_WARN_FLAGS})
target_link_libraries(soulsframe_core PUBLIC tomlplusplus::tomlplusplus xxHash::xxhash)

add_library(soulsframe_probe_core STATIC
  src/probe/jsonl_writer.cpp
  src/probe/trace_types.cpp
)
target_include_directories(soulsframe_probe_core PUBLIC src)
target_compile_options(soulsframe_probe_core PRIVATE ${SF_WARN_FLAGS})

if(SF_HOST_ONLY AND SF_SANITIZE)
  target_compile_options(soulsframe_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(soulsframe_core PUBLIC -fsanitize=address,undefined)
  target_compile_options(soulsframe_probe_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(soulsframe_probe_core PUBLIC -fsanitize=address,undefined)
endif()

if(SF_HOST_ONLY AND SF_COVERAGE)
  target_compile_options(soulsframe_core PUBLIC --coverage -O0 -g)
  target_link_options(soulsframe_core PUBLIC --coverage)
  target_compile_options(soulsframe_probe_core PUBLIC --coverage -O0 -g)
  target_link_options(soulsframe_probe_core PUBLIC --coverage)
endif()

if(NOT SF_HOST_ONLY)
  add_library(soulsframe SHARED
    src/core/dllmain.cpp
    src/core/log.cpp
    src/core/proxy_dinput8.cpp
    src/core/win_helpers.cpp
    src/gfx/d3d11/swapchain_hook.cpp
  )
  target_include_directories(soulsframe PRIVATE src)
  set_target_properties(soulsframe PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
  )
  target_compile_options(soulsframe PRIVATE ${SF_WARN_FLAGS})
  target_link_libraries(soulsframe PRIVATE
    soulsframe_core
    minhook
    d3d11
    dxgi
    -static
    -static-libgcc
    -static-libstdc++
  )
  target_link_options(soulsframe PRIVATE
    -Wl,--dynamicbase
    -Wl,--high-entropy-va
    -Wl,--nxcompat
  )
  if(SF_PROBE)
    target_compile_definitions(soulsframe PRIVATE SF_PROBE=1)
    target_link_libraries(soulsframe PRIVATE soulsframe_probe_core)
  endif()

  if(SF_BUILD_TESTBED)
    add_executable(d3d11_testbed tests/testbed/d3d11_testbed.cpp)
    target_compile_options(d3d11_testbed PRIVATE ${SF_WARN_FLAGS})
    target_link_libraries(d3d11_testbed PRIVATE
      d3d11
      dxgi
      -static
      -static-libgcc
      -static-libstdc++
    )
    target_link_options(d3d11_testbed PRIVATE
      -Wl,--dynamicbase
      -Wl,--high-entropy-va
      -Wl,--nxcompat
    )
  endif()
else()
  enable_testing()
  add_subdirectory(tests/unit)
endif()
