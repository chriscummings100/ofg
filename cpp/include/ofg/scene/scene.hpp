// Minimal render scene passed from Game into Renderer.
//
// Scene is intentionally small during the renderer ownership migration. It
// gives Renderer an explicit object-query boundary without growing into a full
// ECS or world graph. Renderer converts these objects into transient pass draw
// queues internally.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/property_bag.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace ofg {

class Mesh;

struct RenderObject {
    Mesh* m_mesh{nullptr};
    math::Mat4 m_model{math::mat4_identity()};
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin;
};

class Scene {
public:
    Scene() = default;

    // Returns the scene's main render view.
    [[nodiscard]] const RenderView& main_view() const noexcept {
        return m_main_view;
    }

    // Replaces the scene's main render view.
    void set_main_view(RenderView main_view) noexcept {
        m_main_view = main_view;
    }

    // Adds one renderable object in stable insertion order.
    void add_render_object(RenderObject object) {
        m_render_objects.push_back(std::move(object));
    }

    // Removes renderable objects while retaining storage for future frames.
    void clear() noexcept {
        m_render_objects.clear();
    }

    // Returns renderable objects in their current stable order.
    [[nodiscard]] std::span<const RenderObject> render_objects() const noexcept {
        return m_render_objects;
    }

    // Reports the number of renderable objects.
    [[nodiscard]] std::size_t size() const noexcept {
        return m_render_objects.size();
    }

private:
    RenderView m_main_view{render_view_from_matrix(math::mat4_identity())};
    std::vector<RenderObject> m_render_objects;
};

} // namespace ofg
