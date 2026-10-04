// Small shared RHI compute-to-vertex experiment, used by native readback and a browser proof executable.
#pragma once

#include "resources/mesh.h"
#include <slang-rhi.h>
#include <array>

namespace ofg {
class ComputeVertexProof
{
public:
    static constexpr uint32_t vertexCount = 65;
    // Creates explicitly packed source/output buffers and pipelines, rejecting insufficient device limits.
    ComputeVertexProof(rhi::IDevice* device, rhi::ICommandQueue* queue, rhi::Format format);
    // Orders a translation upload, compute and draw in one submission; never waits for GPU completion.
    void draw(rhi::ITexture* target, float translationX);
    // Returns the output for test-only readback after the caller has waited for completion.
    rhi::IBuffer* output() const { return m_output; }
    // Returns the deterministic source fixture, including attributes the kernel must preserve bit-for-bit.
    static std::array<Vertex, vertexCount> sourceVertices();

private:
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IBuffer> m_source, m_output, m_translation;
    rhi::ComPtr<rhi::IComputePipeline> m_compute;
    rhi::ComPtr<rhi::IRenderPipeline> m_render;
};
} // namespace ofg
