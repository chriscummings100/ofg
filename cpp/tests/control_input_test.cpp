// Doctest coverage for raw control input validation.
//
// Browser hosts collect controls, but C++ validates snapshots before storing
// them on Game and before component updates consume them.
#include "doctest.h"

#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"

#include <limits>

// Verifies default controls are inert and valid.
TEST_CASE("ControlInput defaults to an inert valid snapshot") {
    const ofg::ControlInput input;

    CHECK(input.m_move_x == doctest::Approx(0.0f));
    CHECK(input.m_move_y == doctest::Approx(0.0f));
    CHECK(input.m_move_z == doctest::Approx(0.0f));
    CHECK(input.m_look_delta_x == doctest::Approx(0.0f));
    CHECK(input.m_look_delta_y == doctest::Approx(0.0f));
    CHECK(input.m_look_active == false);
    CHECK(input.m_fast == false);
    CHECK(input.m_slow == false);
    CHECK(input.m_cycle_camera_mode == false);
    CHECK(input.m_toggle_overhead_sun == false);
    CHECK_NOTHROW(ofg::validate_control_input(input));
}

// Verifies validation rejects non-finite numeric fields.
TEST_CASE("ControlInput validation rejects non-finite values") {
    ofg::ControlInput input;
    input.m_move_x = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH_AS(ofg::validate_control_input(input), doctest::Contains("finite"), ofg::EngineError);

    input = ofg::ControlInput{};
    input.m_look_delta_y = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_WITH_AS(ofg::validate_control_input(input), doctest::Contains("finite"), ofg::EngineError);
}
