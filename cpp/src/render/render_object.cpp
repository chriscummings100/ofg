// Bounded render-object extraction shared by pass-specific culling.
#include "ofg/render/render_object.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Converts one extracted render object into the existing DrawList command shape.
DrawCommand draw_command_from_render_object(const RenderObject& object) noexcept {
    DrawCommand command;
    command.m_mesh = object.m_mesh;
    command.m_model = object.m_model;
    command.m_properties = object.m_properties;
    command.m_material_overrides = object.m_material_overrides;
    command.m_sort_origin = object.m_sort_origin;
    return command;
}

} // namespace

// Extracts authored-visible scene mesh renderers into bounded render objects.
void extract_render_objects(const Scene& scene, std::vector<RenderObject>& output, RenderObjectExtractionStats& stats) {
    output.clear();
    stats = RenderObjectExtractionStats{};
    stats.m_scene_mesh_renderer_count = static_cast<std::uint32_t>(scene.mesh_renderer_count());
    output.reserve(scene.mesh_renderer_count() + scene.terrain().chunk_count());

    for (std::size_t index = 0; index < scene.mesh_renderer_count(); ++index) {
        const MeshRenderer* mesh_renderer = scene.get_mesh_renderer(index);
        if (mesh_renderer == nullptr || mesh_renderer->entity() == nullptr) {
            throw EngineError("Scene mesh renderer must have an owning entity.");
        }
        if (!mesh_renderer->visible()) {
            stats.m_invisible_renderer_count += 1U;
            continue;
        }

        Mesh* mesh = mesh_renderer->mesh();
        if (mesh == nullptr) {
            throw EngineError("Visible scene mesh renderer must reference a mesh.");
        }

        const math::Mat4 world_from_renderer = world_from_local(*mesh_renderer->entity());
        const std::vector<MaterialOverride>& material_overrides = mesh_renderer->material_overrides();

        RenderObject object;
        object.m_mesh = mesh;
        object.m_model = world_from_renderer;
        object.m_properties = &mesh_renderer->properties();
        object.m_material_overrides =
            std::span<const MaterialOverride>(material_overrides.data(), material_overrides.size());
        object.m_sort_origin = math::transform_point(world_from_renderer, mesh_renderer->sort_origin_offset());
        object.m_local_bounds = mesh->local_bounds();
        object.m_world_bounds = transform_bounds(object.m_local_bounds, world_from_renderer);
        object.m_scene_mesh_renderer_index = static_cast<std::uint32_t>(index);
        output.push_back(object);
    }

    scene.terrain().extract_render_objects(output);
    stats.m_extracted_object_count = static_cast<std::uint32_t>(output.size());
}

// Appends accepted render objects into a draw list using explicit culling planes.
void append_culled_draws(
    std::span<const RenderObject> objects, CullingPlaneSet planes, DrawList& output, CullingStats& stats) {
    stats = CullingStats{};
    for (const RenderObject& object : objects) {
        stats.m_tested_object_count += 1U;
        if (!intersects_culling_planes(object.m_world_bounds, planes)) {
            stats.m_rejected_object_count += 1U;
            continue;
        }
        output.add(draw_command_from_render_object(object));
        stats.m_accepted_object_count += 1U;
    }
}

} // namespace ofg
