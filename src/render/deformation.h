// Direct RHI compute skinning; shared source data and private instance outputs have separate lifetimes.
#pragma once
#include "render/draw-list.h"
#include "render/deformation-storage.h"
#include <slang-rhi.h>

namespace ofg {
struct MeshGpuData;
class DeformationRenderer
{
public:
    // Retains device/graphics queue; the compute pipeline is compiled on first use.
    DeformationRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Releases GPU state from surviving instance handles so Graphics may initialize again.
    ~DeformationRenderer();
    // Uploads shared influence sets once and submits palette uploads/compute/transitions before scene passes.
    // Mesh vertex/index buffers must already be prepared by Graphics. Does not wait for GPU completion.
    void deform(std::span<const DeformationJob> jobs);
    // Returns a prepared instance vertex buffer, including for integration-test readback.
    static rhi::IBuffer* vertices(const DeformationStorage& storage);

private:
    // Compiles the storage-only Slang compute entry point for the selected backend.
    void initializePipeline();
    // Validates portable limits and prepares shared influences plus private output/palette buffers.
    void prepare(const DeformationJob& job);
    rhi::ComPtr<rhi::IDevice> m_device;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IComputePipeline> m_pipeline;
    std::vector<std::weak_ptr<DeformationStorage>> m_instances;
};
} // namespace ofg
