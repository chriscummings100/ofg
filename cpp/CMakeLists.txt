# OFG C++ migration build graph.
#
# This CMake project builds the portable core library, doctest executable,
# browser Emscripten module, and optional native Dawn render-smoke executable
# from the same renderer data.
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

add_library(ofg_cpp_core
  src/core/frame_state.cpp
  src/render/bootstrap_scene.cpp
  src/runtime/browser_runtime.cpp
  src/runtime/runtime_debug_status.cpp
)

target_include_directories(ofg_cpp_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ofg_cpp_core PUBLIC cxx_std_20)

target_compile_options(ofg_cpp_core PRIVATE ${OFG_CXX_WARNING_OPTIONS})

if(OFG_ENABLE_COVERAGE)
  # Native C++ coverage uses Clang source-based instrumentation.
  if(EMSCRIPTEN)
    message(FATAL_ERROR "OFG_ENABLE_COVERAGE is only supported for native C++ tests.")
  endif()
  if(MSVC)
    if(NOT OFG_CLANG_PROFILE_LIB_DIR)
      message(FATAL_ERROR "OFG_ENABLE_COVERAGE with clang-cl requires OFG_CLANG_PROFILE_LIB_DIR.")
    endif()
    set(OFG_COVERAGE_COMPILE_OPTIONS
      /clang:-fprofile-instr-generate
      /clang:-fcoverage-mapping
    )
    set(OFG_COVERAGE_LINK_OPTIONS
      "/LIBPATH:${OFG_CLANG_PROFILE_LIB_DIR}"
      clang_rt.profile.lib
    )
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
  target_compile_options(ofg_cpp_core PUBLIC ${OFG_COVERAGE_COMPILE_OPTIONS})
endif()

if(OFG_BUILD_TESTS AND NOT EMSCRIPTEN)
  # Native tests exercise portable runtime and scene code through doctest/CTest.
  enable_testing()

  add_executable(ofg_cpp_tests
    tests/bootstrap_scene_test.cpp
    tests/browser_runtime_test.cpp
    tests/frame_state_test.cpp
    tests/runtime_debug_status_test.cpp
    tests/test_main.cpp
  )
  target_link_libraries(ofg_cpp_tests PRIVATE ofg_cpp_core)
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
    src/render/bootstrap_renderer.cpp
    src/render/webgpu_common.cpp
    src/web/browser_game.cpp
    src/web/embind_module.cpp
    src/web/emdawn_probe.cpp
    src/web/webgpu_utils.cpp
  )

  target_link_libraries(ofg_cpp_wasm PRIVATE ofg_cpp_core)
  target_compile_definitions(ofg_cpp_wasm PRIVATE OFG_ENABLE_WEBGPU_RENDERER=1)
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
  if(NOT OFG_DAWN_SOURCE_DIR)
    message(FATAL_ERROR "OFG_BUILD_NATIVE_SMOKE requires OFG_DAWN_SOURCE_DIR.")
  endif()

  set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
  set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_NODE_BINDINGS OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_NULL ON CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)

  add_subdirectory(
    "${OFG_DAWN_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/dawn"
    EXCLUDE_FROM_ALL
  )

  add_executable(ofg_render_smoke_cpp
    src/native/png_writer.cpp
    src/native/render_smoke.cpp
    src/native/render_smoke_main.cpp
    src/render/bootstrap_renderer.cpp
    src/render/webgpu_common.cpp
  )
  target_link_libraries(ofg_render_smoke_cpp
    PRIVATE
      ofg_cpp_core
      dawn::webgpu_dawn
  )
  target_compile_definitions(ofg_render_smoke_cpp PRIVATE OFG_ENABLE_WEBGPU_RENDERER=1)
  target_compile_options(ofg_render_smoke_cpp PRIVATE ${OFG_CXX_WARNING_OPTIONS})
endif()
