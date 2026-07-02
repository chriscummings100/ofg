// CPU skinning data shared by imported model resources and scene renderers.
//
// SkinVertexInfluence stores the four joint influences used by the first OFG
// CPU skinning path. Joint indices are in glTF skin.joints order.
#pragma once

#include <array>
#include <cstdint>

namespace ofg {

struct SkinVertexInfluence {
    std::array<std::uint32_t, 4> m_joint_indices{};
    std::array<float, 4> m_weights{};
};

struct SkinningCounters {
    std::uint64_t m_vertices_skinned{0};
    std::uint64_t m_vertex_upload_bytes{0};
    std::uint64_t m_dynamic_vertex_buffer_create_count{0};
};

} // namespace ofg
