// Renderer-facing light extraction helpers.
#include "ofg/render/lighting.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/environment.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/scene.hpp"

#include <optional>
#include <span>
#include <string>

namespace ofg {
namespace {

// Extracts the world-space +Z direction from a light entity transform.
math::Vec3 directional_light_forward(const Entity& entity) {
    const math::Mat4 world_from_light = world_from_local(entity);
    const math::Vec3 forward = math::vec3(world_from_light[2].x, world_from_light[2].y, world_from_light[2].z);
    std::string error;
    const std::optional<math::Vec3> normalized = math::normalize(forward, error);
    if (!normalized.has_value()) {
        throw EngineError(error.empty() ? "Directional light entity forward direction must be nonzero." : error);
    }
    return *normalized;
}

} // namespace

// Builds the transient light-property list consumed by renderer passes.
std::size_t build_light_properties(const Scene& scene, std::span<LightProperties> output) {
    if (output.empty()) {
        return 0;
    }

    const Light* light = scene.environment().main_directional_light();
    if (light == nullptr || light->light_type() != LightType::Directional || !light->enabled()) {
        return 0;
    }
    const Entity* entity = light->entity();
    if (entity == nullptr) {
        return 0;
    }

    output[0] = LightProperties{
        LightPropertiesType::Directional, directional_light_forward(*entity), light->color(), light->intensity()};
    return 1;
}

} // namespace ofg
