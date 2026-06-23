// Camera and render-view values shared by OFG renderer passes.
#include "ofg/render/camera.hpp"

namespace ofg {

// Builds a RenderView from an already-composed view-projection matrix.
RenderView render_view_from_matrix(math::Mat4 view_projection) noexcept {
    return RenderView{view_projection};
}

} // namespace ofg
