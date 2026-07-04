// Durable renderer resource creation counter helpers.
#include "ofg/render/renderer_counters.hpp"

namespace ofg {

// Adds one pass or target counter set into an aggregate renderer counter set.
void add_renderer_counters(RendererCounters& total, RendererCounters next) noexcept {
    total.m_pipeline_create_count += next.m_pipeline_create_count;
    total.m_buffer_create_count += next.m_buffer_create_count;
    total.m_texture_create_count += next.m_texture_create_count;
    total.m_texture_view_create_count += next.m_texture_view_create_count;
    total.m_bind_group_layout_create_count += next.m_bind_group_layout_create_count;
    total.m_bind_group_create_count += next.m_bind_group_create_count;
    total.m_shader_module_create_count += next.m_shader_module_create_count;
}

} // namespace ofg
