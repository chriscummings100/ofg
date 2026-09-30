# Build Slang RHI as a native D3D12 library, without its applications or tests.
set(SLANG_RHI_BUILD_TESTS OFF)
set(SLANG_RHI_BUILD_EXAMPLES OFF)
set(SLANG_RHI_INSTALL OFF)

# Keep unused backends and optional vendor integrations out of the build.
set(SLANG_RHI_ENABLE_D3D12 ON)
set(SLANG_RHI_ENABLE_CPU OFF)
set(SLANG_RHI_ENABLE_D3D11 OFF)
set(SLANG_RHI_ENABLE_VULKAN OFF)
set(SLANG_RHI_ENABLE_METAL OFF)
set(SLANG_RHI_ENABLE_CUDA OFF)
set(SLANG_RHI_ENABLE_OPTIX OFF)
set(SLANG_RHI_ENABLE_WGPU OFF)
set(SLANG_RHI_ENABLE_NVAPI OFF)
set(SLANG_RHI_ENABLE_AFTERMATH OFF)

add_subdirectory(slang-rhi)
