// Baked environment uploads and bounded dynamic IBL jobs; retains only frozen lighting inputs between frames.
#pragma once
#include "resources/environment.h"
#include <slang-rhi.h>
#include "render/draw-list.h"
#include <chrono>
namespace ofg {
class SkyRenderer;
class EnvironmentRenderer
{
public:
    // Retains the device used for immutable environment uploads.
    EnvironmentRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Releases surviving asset GPU handles after the host drains submitted work.
    ~EnvironmentRenderer();
    // Uploads all filters and the lookup atomically; failed preparation leaves the resource unchanged.
    void prepare(Environment& environment);
    // Call once per rendered outdoor frame after sky.update(), using the same SkyRenderer throughout a job.
    // Submits one capture face or one filtered face/mip chain; publishes only complete frozen generations.
    void update(const DrawList& frame, SkyRenderer& sky);
    // Returns the dynamic texture set; callers must check hasDynamic() before sampling it.
    const EnvironmentGpuData& dynamic() const { return *m_dynamic; }
    // Returns previous/current generation indices and the current interpolation weight.
    math::Vec3 blend() const;
    // Compensates each complete generation for clock-driven exposure changes while its radiance is stale.
    // Uses current exposure controls so manual EV/multiplier edits still take effect immediately.
    math::Vec2 exposureScales(const DrawList& frame) const;
    // Returns whether a complete dynamic generation has been published.
    bool hasDynamic() const { return !m_first; }
    // Returns the source snapshot age in seconds for diagnostics.
    double ageSeconds() const;
    // Returns submitted job steps (of 24), or zero when idle; independent of GPU completion.
    uint32_t completedSteps() const { return m_step; }
    // Returns the number of IBL raster passes submitted by the last update (0, 1 or 7).
    uint32_t lastPassCount() const { return m_lastPassCount; }
    // Returns the number of complete generations submitted on this device.
    uint32_t publicationCount() const { return m_publications; }

private:
    // Allocates reusable double-buffered filters and compiles the filter pipeline lazily.
    void initialize();
    // Filters all seven mips of one face into the inactive generation.
    void filterFace(uint32_t kind, uint32_t face);
    rhi::ComPtr<rhi::IDevice> m_device;
    std::vector<std::weak_ptr<EnvironmentGpuData>> m_environments;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IShaderProgram> m_filterProgram;
    rhi::ComPtr<rhi::IRenderPipeline> m_filterPipeline;
    rhi::ComPtr<rhi::ISampler> m_filterSampler;
    std::shared_ptr<EnvironmentGpuData> m_dynamic;
    std::array<float, 20> m_snapshot{};
    std::array<float, 20> m_pendingSnapshot{};
    DrawList m_captureFrame;
    rhi::ComPtr<rhi::ITexture> m_source;
    std::chrono::steady_clock::time_point m_published{};
    std::chrono::steady_clock::time_point m_started{}, m_sourceTime{};
    uint32_t m_generation = 0;
    uint32_t m_targetGeneration = 0;
    std::array<float, 2> m_sunHeights{};
    uint32_t m_step = 0, m_lastPassCount = 0, m_publications = 0;
    bool m_building = false, m_pendingSnapBlend = true;
    bool m_snapBlend = true;
    bool m_first = true;
};
} // namespace ofg
