// Validation for raw per-frame control input.
#include "ofg/core/control_input.hpp"

#include "ofg/core/engine_error.hpp"

#include <cmath>

namespace ofg {

// Validates that numeric controls are finite before storage or use.
void validate_control_input(ControlInput input) {
    if (!std::isfinite(input.m_move_x) || !std::isfinite(input.m_move_y) || !std::isfinite(input.m_move_z) ||
        !std::isfinite(input.m_look_delta_x) || !std::isfinite(input.m_look_delta_y)) {
        throw EngineError("Control input values must be finite.");
    }
}

} // namespace ofg
