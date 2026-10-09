# Tests use a separately pinned header so the CPU preset needs no RHI checkout.
if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_library(doctest INTERFACE)
    target_include_directories(doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)
endif()

if(NOT OFG_BUILD_APP)
    return()
endif()

# Build only the selected platform's RHI backend, without upstream applications or tests.
set(SLANG_RHI_BUILD_TESTS OFF)
set(SLANG_RHI_BUILD_EXAMPLES OFF)
set(SLANG_RHI_INSTALL OFF)

# Keep unused backends and optional vendor integrations out of the build.
set(SLANG_RHI_ENABLE_D3D12 OFF)
set(SLANG_RHI_ENABLE_CPU OFF)
set(SLANG_RHI_ENABLE_D3D11 OFF)
set(SLANG_RHI_ENABLE_VULKAN OFF)
set(SLANG_RHI_ENABLE_METAL OFF)
set(SLANG_RHI_ENABLE_CUDA OFF)
set(SLANG_RHI_ENABLE_OPTIX OFF)
set(SLANG_RHI_ENABLE_WGPU OFF)
set(SLANG_RHI_ENABLE_NVAPI OFF)
set(SLANG_RHI_ENABLE_AFTERMATH OFF)

if(EMSCRIPTEN)
    set(SLANG_RHI_ENABLE_WGPU ON)
    # Release WASM archives were built without atomics and cannot link into shared memory.
    # Build the identical pinned version with tools/build-slang-web.ps1 before configuring web.
    set(SLANG_RHI_FETCH_SLANG OFF)
    set(SLANG_RHI_SLANG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/build/slang-web/Release/include" CACHE STRING "" FORCE)
    set(SLANG_RHI_SLANG_BINARY_DIR "${CMAKE_SOURCE_DIR}/build/slang-web" CACHE STRING "" FORCE)
    if(NOT EXISTS "${SLANG_RHI_SLANG_BINARY_DIR}/lib/libslang-compiler.a")
        message(FATAL_ERROR "Threaded Slang is missing. Set EMSDK and run tools/build-slang-web.ps1 first.")
    endif()
else()
    set(SLANG_RHI_ENABLE_D3D12 ON)
endif()
add_subdirectory(slang-rhi)
if(EMSCRIPTEN)
    include(${CMAKE_SOURCE_DIR}/cmake/rhi-webgpu-formats.cmake)
    include(${CMAKE_SOURCE_DIR}/cmake/rhi-webgpu-lighting.cmake)
else()
    include(${CMAKE_SOURCE_DIR}/cmake/rhi-d3d12-bindings.cmake)
endif()

# Use the same GLFW revision as RHI's native examples, without building those examples.
if(NOT EMSCRIPTEN)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_BUILD_TESTS OFF)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_INSTALL OFF)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG b00e6a8a88ad1b60c0a045e696301deb92c9a13e
    )
    FetchContent_MakeAvailable(glfw)
endif()

# ImGui's docking revision is pinned by the submodule, independently of platform renderer APIs.
add_library(imgui STATIC imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
if(NOT EMSCRIPTEN)
    target_sources(imgui PRIVATE imgui/backends/imgui_impl_glfw.cpp)
    target_link_libraries(imgui PRIVATE glfw)
endif()
