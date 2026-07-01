// Debug fly camera controller for scene-owned cameras.
//
// The controller consumes raw debug input snapshots, derives yaw/pitch from the
// active scene camera, and mutates that camera entity's LocalTransform. It owns
// no renderer or browser state; hosts only provide the latest input values.
#pragma once

#include "ofg/scene/camera.hpp"

namespace ofg {

class Scene;

struct DebugCameraInput {
    float move_x{0.0f};
    float move_y{0.0f};
    float move_z{0.0f};
    float look_delta_x{0.0f};
    float look_delta_y{0.0f};
    bool look_active{false};
    bool fast{false};
    bool slow{false};
};

// Validates that the numeric input fields are finite before storage or use.
void validate_debug_camera_input(DebugCameraInput input);

class DebugCameraController {
public:
    // Restores initial timing/orientation capture state.
    void reset() noexcept;
    // Applies one input snapshot to the scene's active camera.
    void update(Scene& scene, DebugCameraInput input, double time_ms);

private:
    const Camera* m_tracked_camera{nullptr};
    double m_last_time_ms{0.0};
    float m_yaw_radians{0.0f};
    float m_pitch_radians{0.0f};
    bool m_has_last_time{false};
    bool m_has_orientation{false};
};

} // namespace ofg
