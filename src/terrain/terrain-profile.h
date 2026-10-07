// Opt-in diagnostic wall timers for terrain profiling; normal builds compile every scope away.
#pragma once

#ifdef OFG_TERRAIN_FUNCTION_PROFILE
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ostream>

namespace ofg::terrainProfile {
enum class Function
{
    TerrainLaboratory_update,
    TerrainLaboratory_updateRoots,
    TerrainLaboratory_receiveResults,
    TerrainLaboratory_upload,
    TerrainLaboratory_append,
    TerrainStream_reconcile,
    TerrainStream_makePlan,
    TerrainStream_schedule,
    TerrainStream_publish,
    TerrainStream_prune,
    TerrainStream_diagnostics,
    TerrainMesher_step,
    TerrainMesher_construct,
    Graphics_render,
    Graphics_prepareStreamingMesh,
    bindMaterial,
    bindResources,
    bindFrame,
    bindOutdoor,
    outputPass,
    pipelineFor,
    SkyRenderer_update,
    SkyRenderer_drawBackground,
    SkyRenderer_captureFace,
    EnvironmentRenderer_update,
    EnvironmentRenderer_filterFace,
    ShadowRenderer_render,
    ShadowRenderer_prepareMaterial,
    GpuWait,
    DrawBindings,
    FinishScene,
    SubmitScene,
    Count
};
inline constexpr std::array names{
    "TerrainLaboratory::update",
    "TerrainLaboratory::updateRoots",
    "TerrainLaboratory::receiveResults",
    "TerrainLaboratory::upload",
    "TerrainLaboratory::append",
    "TerrainStream::reconcile",
    "TerrainStream::makePlan",
    "TerrainStream::schedule",
    "TerrainStream::publish",
    "TerrainStream::prune",
    "TerrainStream::diagnostics",
    "TerrainMesher::step",
    "TerrainMesher::TerrainMesher",
    "Graphics::render",
    "Graphics::prepareStreamingMesh",
    "bindMaterial",
    "bindResources",
    "bindFrame",
    "bindOutdoor",
    "outputPass",
    "pipelineFor",
    "SkyRenderer::update",
    "SkyRenderer::drawBackground",
    "SkyRenderer::captureFace",
    "EnvironmentRenderer::update",
    "EnvironmentRenderer::filterFace",
    "ShadowRenderer::render",
    "ShadowRenderer::prepareMaterial",
    "queue.waitOnHost",
    "Graphics.createDrawBindings",
    "Graphics.finishScene",
    "Graphics.submitScene",
};
static_assert(names.size() == size_t(Function::Count));

struct Counter
{
    std::atomic<uint64_t> calls{0}, inclusive{0}, exclusive{0};
};
inline std::array<Counter, size_t(Function::Count)> counters;
inline std::atomic<bool> enabled{false};

class Scope
{
public:
    // Starts a thread-local nested wall timer only during an explicitly enabled diagnostic run.
    explicit Scope(Function function)
        : m_function(function)
        , m_enabled(enabled.load(std::memory_order_relaxed))
    {
        if (m_enabled)
        {
            m_parent = current;
            current = this;
            m_start = std::chrono::steady_clock::now();
        }
    }
    // Records completed calls; exclusive time subtracts only instrumented children on this thread.
    ~Scope()
    {
        if (!m_enabled)
        {
            return;
        }
        const auto elapsed = uint64_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start).count()
        );
        current = m_parent;
        if (m_parent)
        {
            m_parent->m_children += elapsed;
        }
        auto& counter = counters[size_t(m_function)];
        counter.calls.fetch_add(1, std::memory_order_relaxed);
        counter.inclusive.fetch_add(elapsed, std::memory_order_relaxed);
        counter.exclusive.fetch_add(elapsed - m_children, std::memory_order_relaxed);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    inline static thread_local Scope* current = nullptr;
    Function m_function;
    bool m_enabled;
    Scope* m_parent = nullptr;
    std::chrono::steady_clock::time_point m_start;
    uint64_t m_children = 0;
};

// Writes cumulative completed-call totals. Worker calls crossing checkpoints belong to their completion interval.
inline void checkpoint(std::ostream& output, const char* phase, size_t frames, double seconds)
{
    for (size_t i = 0; i < counters.size(); ++i)
    {
        const auto& c = counters[i];
        output << phase << ',' << frames << ',' << seconds << ',' << names[i] << ',' << c.calls.load() << ','
               << double(c.inclusive.load()) / 1e6 << ',' << double(c.exclusive.load()) / 1e6 << '\n';
    }
}
} // namespace ofg::terrainProfile
#define OFG_TERRAIN_SCOPE(name) ofg::terrainProfile::Scope terrainProfileScope(ofg::terrainProfile::Function::name)
#else
#define OFG_TERRAIN_SCOPE(name) ((void)0)
#endif
