// Camera and render-view values shared by OFG renderer passes.
//
// Milestone 3 only needs a packed view-projection matrix. Later scene code can
// layer camera controllers on top without changing renderer pass inputs.
#pragma once

#include "ofg/math/mat.hpp"

namespace ofg {

struct RenderView {
    math::Mat4 m_view_projection;
};

// Builds a RenderView from an already-composed view-projection matrix.
[[nodiscard]] RenderView render_view_from_matrix(math::Mat4 view_projection) noexcept;

} // namespace ofg
