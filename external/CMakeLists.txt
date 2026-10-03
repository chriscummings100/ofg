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
else()
    set(SLANG_RHI_ENABLE_D3D12 ON)
endif()
add_subdirectory(slang-rhi)

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
