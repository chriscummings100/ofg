// High-level OFG renderer that consumes resolved draw lists.
#include "ofg/render/renderer.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ofg {

// Stores the created opaque pass.
Renderer::Renderer(std::unique_ptr<OpaquePass> opaque_pass) : m_opaque_pass(std::move(opaque_pass)) {}

// Releases pass resources.
Renderer::~Renderer() = default;

// Creates the pass graph for one WebGPU device and color target format.
std::unique_ptr<Renderer> Renderer::create(GpuContext gpu, WGPUTextureFormat color_format, std::string& error) {
    std::unique_ptr<OpaquePass> opaque_pass = OpaquePass::create(std::move(gpu), color_format, error);
    if (!opaque_pass) {
        return nullptr;
    }
    error.clear();
    return std::unique_ptr<Renderer>(new Renderer(std::move(opaque_pass)));
}

// Prepares lazy pipelines for the currently owned draw list.
bool Renderer::prepare(const DrawList& draw_list, std::string& error) {
    return m_opaque_pass->prepare(draw_list, error);
}

// Resizes pass-level render targets.
bool Renderer::resize(std::uint32_t width, std::uint32_t height, std::string& error) {
    return m_opaque_pass->resize(width, height, error);
}

// Records all renderer passes into the caller-owned command encoder.
bool Renderer::render(WGPUCommandEncoder encoder,
    RenderTarget target,
    const RenderView& view,
    const DrawList& draw_list,
    std::string& error) {
    return m_opaque_pass->render(encoder, target, view, draw_list, error);
}

// Reports durable resource creation counters.
RendererCounters Renderer::counters() const noexcept {
    return m_opaque_pass->counters();
}

} // namespace ofg
