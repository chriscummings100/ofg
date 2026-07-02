// Scene component update context.
//
// Game creates one context per accepted frame. Components read the latest
// validated control snapshot and shared frame timing from this context while
// mutating their own owning entity transforms.
#pragma once

#include "ofg/core/control_input.hpp"

namespace ofg {

class Camera;
class Player;

struct SceneUpdateContext {
    const ControlInput& m_controls;
    double m_time_ms{0.0};
    float m_delta_seconds{0.0f};
    Player* m_primary_player{nullptr};
    Camera* m_main_camera{nullptr};
};

} // namespace ofg
