// Durable renderer resource creation counters used by tests and debug status.
//
// These counters are intentionally cumulative for one renderer/device lifetime.
// They let tests prove that steady-state frames update buffers and submit draws
// without recreating durable GPU resources such as pipelines, textures, bind
// group layouts, shader modules, or size-independent bind groups.
#pragma once

#include <cstdint>

namespace ofg {

struct RendererCounters {
    std::uint32_t m_pipeline_create_count{0};
    std::uint32_t m_buffer_create_count{0};
    std::uint32_t m_texture_create_count{0};
    std::uint32_t m_texture_view_create_count{0};
    std::uint32_t m_bind_group_layout_create_count{0};
    std::uint32_t m_bind_group_create_count{0};
    std::uint32_t m_shader_module_create_count{0};
};

// Adds one pass or target counter set into an aggregate renderer counter set.
void add_renderer_counters(RendererCounters& total, RendererCounters next) noexcept;

} // namespace ofg
