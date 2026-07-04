// CPU-visible diagnostics for the cascaded shadow-map renderer path.
//
// These values are produced by the shadow caster pass before opaque sampling is
// wired in. They let tests and later debug status verify caster culling, pass
// encoding, map size, and low-sun behavior without inspecting private WebGPU
// handles.
#pragma once

#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstdint>

namespace ofg {

struct ShadowCascadeDiagnostics {
    std::uint32_t m_index{0};
    std::uint32_t m_tested_caster_count{0};
    std::uint32_t m_accepted_caster_count{0};
    std::uint32_t m_rejected_caster_count{0};
    std::uint32_t m_draw_count{0};
    std::uint32_t m_submesh_count{0};
    std::uint32_t m_index_count{0};
};

struct ShadowPassDiagnostics {
    bool m_enabled{false};
    std::uint32_t m_cascade_count{0};
    std::uint32_t m_encoded_pass_count{0};
    std::uint32_t m_map_size{0};
    std::uint64_t m_estimated_depth_bytes{0};
    ShadowPcfMode m_pcf_mode{ShadowPcfMode::Hard};
    std::uint32_t m_pcf_sample_count{1};
    float m_sun_elevation_radians{0.0f};
    float m_effective_intensity{0.0f};
    bool m_low_sun_clamped{false};
    std::array<ShadowCascadeDiagnostics, shadow_cascade_count()> m_cascades{};
    std::uint32_t m_total_tested_caster_count{0};
    std::uint32_t m_total_accepted_caster_count{0};
    std::uint32_t m_total_rejected_caster_count{0};
    std::uint32_t m_total_draw_count{0};
    std::uint32_t m_total_submesh_count{0};
    std::uint32_t m_total_index_count{0};
};

} // namespace ofg
