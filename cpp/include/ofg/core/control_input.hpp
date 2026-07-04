// Per-frame raw control input consumed by C++ gameplay and camera components.
//
// Browser code owns DOM event collection, while C++ owns interpretation. This
// snapshot uses stable named axes and one-frame action edges so components can
// decide which controls matter for their current mode.
#pragma once

namespace ofg {

struct ControlInput {
    float m_move_x{0.0f};
    float m_move_y{0.0f};
    float m_move_z{0.0f};
    float m_look_delta_x{0.0f};
    float m_look_delta_y{0.0f};
    bool m_look_active{false};
    bool m_fast{false};
    bool m_slow{false};
    bool m_cycle_camera_mode{false};
    bool m_toggle_overhead_sun{false};
};

// Validates that numeric controls are finite before storage or use.
void validate_control_input(ControlInput input);

} // namespace ofg
