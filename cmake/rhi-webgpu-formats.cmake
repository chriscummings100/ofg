# Adapt only the pinned WebGPU capability report, using generated source rather than editing the submodule.
# Float texture bindings in this revision require WebGPU's filterable-float sample type.
set(ofg_wgpu_source "${CMAKE_CURRENT_SOURCE_DIR}/slang-rhi/src/wgpu/wgpu-device.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ofg_wgpu_source}")
file(READ "${ofg_wgpu_source}" ofg_wgpu_text)
set(ofg_wgpu_anchor "        m_formatSupport[size_t(format)] = support;")
string(FIND "${ofg_wgpu_text}" "${ofg_wgpu_anchor}" ofg_wgpu_anchor_offset)
if(ofg_wgpu_anchor_offset EQUAL -1)
    message(FATAL_ERROR "Pinned RHI format-report patch no longer applies; review upstream changes.")
endif()
string(REPLACE "${ofg_wgpu_anchor}" "
        // OFG: the pinned binding layout cannot bind unfilterable float32 textures.
        if (!supportFloat32Filterable &&
            (format == Format::R32Float || format == Format::RG32Float || format == Format::RGBA32Float))
        {
            support &= ~FormatSupport::ShaderSample;
        }
${ofg_wgpu_anchor}" ofg_wgpu_text "${ofg_wgpu_text}")
set(ofg_wgpu_patched "${CMAKE_CURRENT_BINARY_DIR}/ofg-wgpu-device.cpp")
file(CONFIGURE OUTPUT "${ofg_wgpu_patched}" CONTENT "${ofg_wgpu_text}" @ONLY)
get_target_property(ofg_rhi_sources slang-rhi SOURCES)
list(REMOVE_ITEM ofg_rhi_sources src/wgpu/wgpu-device.cpp)
set_property(TARGET slang-rhi PROPERTY SOURCES "${ofg_rhi_sources}")
target_sources(slang-rhi PRIVATE "${ofg_wgpu_patched}")
target_include_directories(slang-rhi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/slang-rhi/src/wgpu")
