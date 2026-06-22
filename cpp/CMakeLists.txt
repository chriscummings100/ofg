# OFG C++ migration build graph.
#
# This CMake project builds one shared OFG C++ library, a doctest executable,
# browser Emscripten module, and optional native Dawn render-smoke executable
# from the same WebGPU-capable renderer code.
cmake_minimum_required(VERSION 3.26)

project(ofg_cpp LANGUAGES CXX)

option(OFG_BUILD_TESTS "Build OFG C++ doctest test executables" ON)
option(OFG_BUILD_WASM "Build the OFG browser WebAssembly module" OFF)
option(OFG_BUILD_NATIVE_SMOKE "Build the OFG native Dawn render-smoke executable" OFF)
option(OFG_ENABLE_COVERAGE "Enable Clang source-based coverage for native C++ tests" OFF)
set(OFG_DAWN_SOURCE_DIR "" CACHE PATH "Path to a pinned Dawn source checkout")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "OFG C++ builds are Clang-only. Detected ${CMAKE_CXX_COMPILER_ID}.")
endif()

if(MSVC)
  set(OFG_CXX_WARNING_OPTIONS /W4)
else()
  set(OFG_CXX_WARNING_OPTIONS -Wall -Wextra -Wpedantic)
endif()

set(OFG_NEEDS_NATIVE_WEBGPU OFF)
if((OFG_BUILD_TESTS OR OFG_BUILD_NATIVE_SMOKE) AND NOT EMSCRIPTEN)
  set(OFG_NEEDS_NATIVE_WEBGPU ON)
endif()

if(OFG_NEEDS_NATIVE_WEBGPU)
  if(NOT OFG_DAWN_SOURCE_DIR)
    message(FATAL_ERROR "Native OFG C++ builds require OFG_DAWN_SOURCE_DIR because the shared ofg_cpp library includes WebGPU renderer sources.")
  endif()

  set(DAWN_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_NODE_BINDINGS OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
  set(DAWN_USE_GLFW OFF CACHE BOOL "" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_NULL ON CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
  set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
  set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)

  add_subdirectory(
    "${OFG_DAWN_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/dawn"
    EXCLUDE_FROM_ALL
  )
endif()

add_library(ofg_cpp
  src/core/frame_state.cpp
  src/game/game.cpp
  src/game/game_runtime.cpp
  src/game/render_target.cpp
  src/render/bootstrap_scene.cpp
  src/render/bootstrap_renderer.cpp
  src/render/webgpu_common.cpp
  src/runtime/runtime_debug_status.cpp
)

target_include_directories(ofg_cpp
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ofg_cpp PUBLIC cxx_std_20)

target_compile_options(ofg_cpp PRIVATE ${OFG_CXX_WARNING_OPTIONS})

if(EMSCRIPTEN)
  target_compile_options(ofg_cpp PRIVATE --use-port=emdawnwebgpu)
elseif(OFG_NEEDS_NATIVE_WEBGPU)
  target_link_libraries(ofg_cpp PUBLIC webgpu_c)
endif()

if(OFG_ENABLE_COVERAGE)
  # Native C++ coverage uses Clang source-based instrumentation.
  if(EMSCRIPTEN)
    message(FATAL_ERROR "OFG_ENABLE_COVERAGE is only supported for native C++ tests.")
  endif()
  if(MSVC)
    if(OFG_CLANG_PROFILE_LIB_PATH)
      set(OFG_COVERAGE_PROFILE_LINK "${OFG_CLANG_PROFILE_LIB_PATH}")
    elseif(OFG_CLANG_PROFILE_LIB_DIR)
      set(OFG_COVERAGE_PROFILE_LINK
        "/LIBPATH:${OFG_CLANG_PROFILE_LIB_DIR}"
        clang_rt.profile.lib
      )
    else()
      message(FATAL_ERROR "OFG_ENABLE_COVERAGE with clang-cl requires OFG_CLANG_PROFILE_LIB_PATH or OFG_CLANG_PROFILE_LIB_DIR.")
    endif()
    set(OFG_COVERAGE_COMPILE_OPTIONS
      /clang:-fprofile-instr-generate
      /clang:-fcoverage-mapping
    )
    set(OFG_COVERAGE_LINK_OPTIONS ${OFG_COVERAGE_PROFILE_LINK})
  else()
    set(OFG_COVERAGE_COMPILE_OPTIONS
      -fprofile-instr-generate
      -fcoverage-mapping
    )
    set(OFG_COVERAGE_LINK_OPTIONS
      -fprofile-instr-generate
      -fcoverage-mapping
    )
  endif()
  target_compile_options(ofg_cpp PUBLIC ${OFG_COVERAGE_COMPILE_OPTIONS})
endif()

if(OFG_BUILD_TESTS AND NOT EMSCRIPTEN)
  # Native tests exercise the shared OFG C++ library through doctest/CTest.
  enable_testing()

  add_executable(ofg_cpp_tests
    tests/bootstrap_scene_test.cpp
    tests/frame_state_test.cpp
    tests/game_runtime_test.cpp
    tests/runtime_debug_status_test.cpp
    tests/test_main.cpp
  )
  target_link_libraries(ofg_cpp_tests
    PRIVATE
      ofg_cpp
      dawn::webgpu_dawn
  )
  target_include_directories(ofg_cpp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest
  )
  target_compile_options(ofg_cpp_tests PRIVATE ${OFG_CXX_WARNING_OPTIONS})
  if(OFG_ENABLE_COVERAGE)
    target_compile_options(ofg_cpp_tests PRIVATE ${OFG_COVERAGE_COMPILE_OPTIONS})
    target_link_options(ofg_cpp_tests PRIVATE ${OFG_COVERAGE_LINK_OPTIONS})
  endif()

  add_test(NAME ofg_cpp_tests COMMAND ofg_cpp_tests)
endif()

if(OFG_BUILD_WASM)
  # Browser builds use Emscripten/Embind plus Emdawnwebgpu for WebGPU.
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "OFG_BUILD_WASM requires Emscripten's Clang toolchain.")
  endif()

  add_executable(ofg_cpp_wasm
    src/web/browser_game.cpp
    src/web/embind_module.cpp
    src/web/emdawn_probe.cpp
    src/web/webgpu_utils.cpp
  )

  target_link_libraries(ofg_cpp_wasm PRIVATE ofg_cpp)
  target_compile_options(ofg_cpp_wasm PRIVATE
    ${OFG_CXX_WARNING_OPTIONS}
    --use-port=emdawnwebgpu
  )
  target_link_options(ofg_cpp_wasm PRIVATE
    --bind
    --use-port=emdawnwebgpu
    -sALLOW_MEMORY_GROWTH=0
    -sENVIRONMENT=web
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createOfgCppModule
    -sINITIAL_MEMORY=33554432
    -sMODULARIZE=1
    -sNO_EXIT_RUNTIME=1
  )

  set(OFG_WASM_OUTPUT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/wasm/ofg_cpp"
    CACHE PATH "Directory for generated OFG C++ WebAssembly browser assets"
  )
  set_target_properties(ofg_cpp_wasm PROPERTIES
    OUTPUT_NAME ofg_cpp
    RUNTIME_OUTPUT_DIRECTORY "${OFG_WASM_OUTPUT_DIR}"
    SUFFIX ".js"
  )
endif()

if(OFG_BUILD_NATIVE_SMOKE)
  # Native smoke builds Dawn locally and renders the bootstrap scene offscreen.
  if(EMSCRIPTEN)
    message(FATAL_ERROR "OFG_BUILD_NATIVE_SMOKE requires a native Clang toolchain.")
  endif()

  add_executable(ofg_render_smoke_cpp
    src/native/png_writer.cpp
    src/native/render_smoke.cpp
    src/native/render_smoke_main.cpp
  )
  target_link_libraries(ofg_render_smoke_cpp
    PRIVATE
      ofg_cpp
      dawn::webgpu_dawn
  )
  target_compile_options(ofg_render_smoke_cpp PRIVATE ${OFG_CXX_WARNING_OPTIONS})
endif()
