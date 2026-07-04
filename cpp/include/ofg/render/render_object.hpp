// Bounded render-object extraction shared by pass-specific culling.
//
// A RenderObject is a transient borrow of scene renderer state plus conservative
// local/world bounds. Render passes filter these objects into their own DrawList
// using explicit culling planes.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/frustum.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ofg {

class Scene;

struct RenderObject {
    Mesh* m_mesh{nullptr};
    math::Mat4 m_model{math::mat4_identity()};
    const PropertyBag* m_properties{nullptr};
    std::span<const MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin;
    Bounds3 m_local_bounds{};
    Bounds3 m_world_bounds{};
    std::uint32_t m_scene_mesh_renderer_index{0};
};

struct RenderObjectExtractionStats {
    std::uint32_t m_scene_mesh_renderer_count{0};
    std::uint32_t m_extracted_object_count{0};
    std::uint32_t m_invisible_renderer_count{0};
};

struct CullingStats {
    std::uint32_t m_tested_object_count{0};
    std::uint32_t m_accepted_object_count{0};
    std::uint32_t m_rejected_object_count{0};
};

// Extracts authored-visible scene mesh renderers into bounded render objects.
void extract_render_objects(const Scene& scene, std::vector<RenderObject>& output, RenderObjectExtractionStats& stats);

// Appends accepted render objects into a draw list using explicit culling planes.
void append_culled_draws(
    std::span<const RenderObject> objects, CullingPlaneSet planes, DrawList& output, CullingStats& stats);

} // namespace ofg
