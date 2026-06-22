// Doctest coverage for the portable frame-state counter.
//
// These tests keep the smallest C++ runtime primitive honest before browser or
// WebGPU ownership is layered on top.
#include "doctest.h"

#include "ofg/core/frame_state.hpp"

// Verifies a default FrameState starts with no accepted frames.
TEST_CASE("FrameState starts at frame zero") {
    const ofg::FrameState state;

    CHECK(state.frame_count() == 0);
    CHECK(state.last_time_ms() == 0.0);
}

// Verifies that accepted ticks both increment count and update last timestamp.
TEST_CASE("FrameState records ticks and last time") {
    ofg::FrameState state;

    state.tick(16.5);
    state.tick(33.0);

    CHECK(state.frame_count() == 2);
    CHECK(state.last_time_ms() == doctest::Approx(33.0));
}
