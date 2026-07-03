// Scene component update context.
//
// Game creates one context per accepted frame. Components read the latest
// validated control snapshot and shared frame timing from this context while
// mutating their own owning entity transforms.
#pragma once

#include "ofg/core/control_input.hpp"
#include "ofg/game/gpu_context.hpp"

#include <utility>

namespace ofg {

class Camera;
class Player;
class Scene;

struct SceneUpdateContext {
    // Stores per-frame inputs plus optional scene/resource context for loaders.
    SceneUpdateContext(const ControlInput& controls,
        double time_ms,
        float delta_seconds,
        Player* primary_player,
        Camera* main_camera,
        Scene* scene = nullptr,
        GpuContext gpu = {}) noexcept
        : m_controls(controls), m_time_ms(time_ms), m_delta_seconds(delta_seconds), m_primary_player(primary_player),
          m_main_camera(main_camera), m_scene(scene), m_gpu(std::move(gpu)) {}

    const ControlInput& m_controls;
    double m_time_ms{0.0};
    float m_delta_seconds{0.0f};
    Player* m_primary_player{nullptr};
    Camera* m_main_camera{nullptr};
    Scene* m_scene{nullptr};
    GpuContext m_gpu;
};

} // namespace ofg
