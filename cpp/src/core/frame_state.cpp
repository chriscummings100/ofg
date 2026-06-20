// Frame timing state implementation for the portable C++ runtime core.
#include "ofg/core/frame_state.hpp"

namespace ofg {

// Records one accepted frame timestamp and increments the frame counter.
void FrameState::tick(double time_ms) {
  ++frame_count_;
  last_time_ms_ = time_ms;
}

// Returns how many frames have been accepted by this state object.
std::uint64_t FrameState::frame_count() const noexcept {
  return frame_count_;
}

// Returns the most recent accepted frame timestamp in milliseconds.
double FrameState::last_time_ms() const noexcept {
  return last_time_ms_;
}

} // namespace ofg
