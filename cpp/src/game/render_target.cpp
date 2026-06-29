// Validation helpers for shared game render targets.
#include "ofg/game/render_target.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <cstdint>
#include <sstream>

namespace ofg {

// Validates a target against the renderer format and latest accepted size.
void validate_render_target(RenderTarget target,
    WGPUTextureFormat expected_format,
    std::uint32_t expected_width,
    std::uint32_t expected_height) {
    if (target.m_view == nullptr) {
        throw EngineError("Game render requires a texture view.");
    }
    if (target.m_format != expected_format) {
        throw EngineError("Game render target format " + gpu::texture_format_name(target.m_format) +
                          " does not match renderer format " + gpu::texture_format_name(expected_format) + ".");
    }
    if (target.m_width == 0 || target.m_height == 0) {
        throw EngineError("Game render target dimensions must be nonzero.");
    }
    if (target.m_width != expected_width || target.m_height != expected_height) {
        std::ostringstream out;
        out << "Game render target size " << target.m_width << "x" << target.m_height
            << " does not match latest resize " << expected_width << "x" << expected_height << ".";
        throw EngineError(out.str());
    }
}

} // namespace ofg
