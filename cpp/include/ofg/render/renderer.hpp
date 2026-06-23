// High-level OFG renderer that consumes resolved draw lists.
//
// Renderer is the shared C++ render entry used by Game. Platform frame drivers
// still own target acquisition, command-buffer finish, queue submit, and
// presentation.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/opaque_pass.hpp"

#include <memory>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

class Renderer {
public:
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    // Creates the pass graph for one WebGPU device and color target format.
    [[nodiscard]] static std::unique_ptr<Renderer> create(
        GpuContext gpu, WGPUTextureFormat color_format, std::string& error);
    // Prepares lazy pipelines for the currently owned draw list.
    [[nodiscard]] bool prepare(const DrawList& draw_list, std::string& error);
    // Resizes pass-level render targets.
    [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height, std::string& error);
    // Records all renderer passes into the caller-owned command encoder.
    [[nodiscard]] bool render(WGPUCommandEncoder encoder,
        RenderTarget target,
        const RenderView& view,
        const DrawList& draw_list,
        std::string& error);
    // Reports durable resource creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores the created opaque pass.
    explicit Renderer(std::unique_ptr<OpaquePass> opaque_pass);

    std::unique_ptr<OpaquePass> m_opaque_pass;
};

} // namespace ofg
