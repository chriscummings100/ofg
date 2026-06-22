// Frame timing state for the C++ runtime core.
//
// This header owns the small, renderer-agnostic frame counter that the browser
// facade and debug status expose. It intentionally stores only deterministic
// timing facts so native tests can validate runtime behavior without WebGPU.
#pragma once

#include <cstdint>

namespace ofg {

class FrameState {
public:
    // Records one accepted frame timestamp and increments the frame counter.
    void tick(double time_ms);

    // Returns how many frames have been accepted by this state object.
    [[nodiscard]] std::uint64_t frame_count() const noexcept;
    // Returns the most recent accepted frame timestamp in milliseconds.
    [[nodiscard]] double last_time_ms() const noexcept;

private:
    std::uint64_t m_frame_count{0};
    double m_last_time_ms{0.0};
};

} // namespace ofg
