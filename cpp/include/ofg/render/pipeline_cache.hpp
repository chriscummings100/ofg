// Render pipeline cache for OFG opaque draw submission.
//
// Pipelines are keyed by shader/material layout state and target formats rather
// than by generic resource ownership. The cache owns WebGPU pipeline handles and
// releases them when the renderer is destroyed.
#pragma once

#include <cstdint>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

struct PipelineKey {
    WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
    WGPUTextureFormat m_depth_format{WGPUTextureFormat_Undefined};
    WGPUBindGroupLayout m_material_layout{nullptr};
    WGPUBindGroupLayout m_shadow_layout{nullptr};
    std::uint64_t m_shader_revision{0};
};

struct PipelineCacheCounters {
    std::uint32_t m_pipeline_create_count{0};
};

// Compares two pipeline keys by render-state identity.
[[nodiscard]] bool operator==(const PipelineKey& left, const PipelineKey& right) noexcept;

class PipelineCache {
public:
    PipelineCache() = default;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&& other) noexcept;
    PipelineCache& operator=(PipelineCache&& other) noexcept;
    ~PipelineCache();

    // Returns an existing pipeline or creates one for the supplied layouts.
    [[nodiscard]] WGPURenderPipeline get_or_create(WGPUDevice device,
        PipelineKey key,
        WGPUBindGroupLayout frame_layout,
        WGPUBindGroupLayout draw_layout,
        WGPUShaderModule shader_module);
    // Releases all cached pipelines.
    void clear() noexcept;
    // Reports cache creation counters.
    [[nodiscard]] PipelineCacheCounters counters() const noexcept;

private:
    struct Entry {
        PipelineKey m_key;
        WGPURenderPipeline m_pipeline{nullptr};
    };

    std::vector<Entry> m_entries;
    PipelineCacheCounters m_counters;
};

} // namespace ofg
