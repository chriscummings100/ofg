// Validation helpers for shared game render targets.
#include "ofg/game/render_target.hpp"

#include "ofg/render/webgpu_common.hpp"

#include <cstdint>
#include <sstream>
#include <string>

namespace ofg {

// Validates a target against the renderer format and latest accepted size.
bool validate_render_target(RenderTarget target,
    WGPUTextureFormat expected_format,
    std::uint32_t expected_width,
    std::uint32_t expected_height,
    std::string& error) {
    if (target.m_view == nullptr) {
        error = "Game render requires a texture view.";
        return false;
    }
    if (target.m_format != expected_format) {
        error = "Game render target format " + gpu::texture_format_name(target.m_format) +
                " does not match renderer format " + gpu::texture_format_name(expected_format) + ".";
        return false;
    }
    if (target.m_width == 0 || target.m_height == 0) {
        error = "Game render target dimensions must be nonzero.";
        return false;
    }
    if (target.m_width != expected_width || target.m_height != expected_height) {
        std::ostringstream out;
        out << "Game render target size " << target.m_width << "x" << target.m_height
            << " does not match latest resize " << expected_width << "x" << expected_height << ".";
        error = out.str();
        return false;
    }
    error.clear();
    return true;
}

} // namespace ofg
