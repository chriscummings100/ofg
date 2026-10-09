// Terrain inspection controls; this translation unit keeps ImGui out of the rendering library.
#include "lab/terrain-laboratory.h"
#include <imgui.h>
namespace ofg {
void TerrainLaboratory::panel()
{
    ImGui::Begin("Terrain streaming");
    if (!m_launch.serviceUrl.empty())
    {
        ImGui::TextWrapped("Service: %s / %s", m_launch.serviceUrl.c_str(), m_launch.island.c_str());
        if (ImGui::Checkbox("Skip terrain cache", &m_launch.skipCache) && m_service)
            m_service->setSkipCache(m_launch.skipCache);
        if (m_manifest)
        {
            if (ImGui::Button("Frame island"))
                frameIsland();
            ImGui::Checkbox("Sea-level reference", &m_seaReference);
            ImGui::TextWrapped("Revision: %s; source %.0f m", m_manifest->revision.c_str(), m_manifest->sourceSpacing);
            ImGui::Text("Cache: %zu hits / %zu misses / %zu bypasses", m_cacheHits, m_cacheMisses, m_cacheBypasses);
            if (ImGui::Combo("Terrain channel", &m_terrainChannel, "Shaded\0Height\0Sediment\0Water depth\0Material\0"))
                for (auto& [id, payload] : m_payloads)
                    if (payload.texture)
                        payload.material->setUniform("terrainChannel", uint32_t(m_terrainChannel));
        }
        if (!m_cacheWarning.empty())
            ImGui::TextWrapped("Cache: %s", m_cacheWarning.c_str());
    }
    if (!m_service)
    {
        ImGui::TextUnformatted(m_manifestRequest ? "Connecting..." : "Terrain connection failed");
        if (!m_error.empty())
            ImGui::TextWrapped("%s", m_error.c_str());
        if (!m_manifestRequest && ImGui::Button("Retry connection"))
            requestManifest();
        ImGui::End();
        return;
    }
    ImGui::Checkbox("Freeze observer", &m_freeze);
    ImGui::Checkbox("LOD colors", &m_lodColors);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Cell widths halve from 131072 m to 16 m across 14 levels");
    }
    ImGui::Checkbox("Node bounds", &m_bounds);
    ImGui::Checkbox("Hold next job", &m_holdNext);
    ImGui::Checkbox("Fail next job", &m_failNext);
    if (ImGui::Button("Release held jobs"))
    {
        m_service->releaseHeld();
    }
    ImGui::SameLine();
    if (ImGui::Button("Retry failures"))
    {
        m_service->retryFailures();
        m_error.clear();
    }
    if (ImGui::Button("Origin"))
    {
        teleportToSurface({{}, {0, 0, -300}});
    }
    ImGui::SameLine();
    if (ImGui::Button("Distant negative"))
    {
        teleportToSurface({{-1000000000, 0, -1000000000}, {512, 0, 512}});
    }
    if (!m_manifest && ImGui::Button("Reseed"))
    {
        ++m_generator.seed;
        m_service->reset(m_generator);
    }
    const auto d = diagnostics();
    ImGui::TextWrapped("%.1f fps; terrain update %.2f ms", double(ImGui::GetIO().Framerate), m_updateMilliseconds);
    ImGui::TextWrapped("Streaming worker %.2f ms; refinement radius 4096 m", m_reconciliationMilliseconds);
    ImGui::TextWrapped(
        "Finest surface cell %u m; %zu leaves want refinement%s",
        unsigned(m_settings.rootWidth) >> d.deepestSurfaceDepth,
        d.unresolvedRefinements,
        d.planningIdle ? "; planner idle" : ""
    );
    ImGui::TextWrapped("Roots %zu admitted / %zu loading; cut %zu", d.admittedRoots, d.loadingRoots, d.selected);
    ImGui::TextWrapped("Nodes %zu; jobs %zu; failed %zu", d.nodes, d.jobs, d.failed);
    ImGui::TextWrapped(
        "CPU payload %.1f / %zu MiB",
        double(d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes) / (1 << 20),
        m_settings.cpuBudget >> 20
    );
    ImGui::TextUnformatted("Worker scratch: up to 32 MiB; upload staging: 16 MiB");
    if (m_manifest)
        ImGui::Text(
            "Source %.1f MiB; texture %.1f MiB per CPU/GPU",
            double(d.sourceCpuBytes) / (1 << 20),
            double(d.sourceTextureBytes) / (1 << 20)
        );
    ImGui::TextWrapped(
        "GPU %.1f MiB; retired %.1f MiB",
        double(d.residentGpuBytes + d.reservedGpuBytes + d.retiredGpuBytes) / (1 << 20),
        double(d.retiredGpuBytes) / (1 << 20)
    );
    ImGui::TextWrapped(
        "Publications %llu; stale results %llu%s",
        (unsigned long long)d.publications,
        (unsigned long long)d.staleResults,
        d.budgetBlocked ? "; waiting for budget" : ""
    );
    ImGui::TextWrapped(
        "Cell %lld, %lld, %lld",
        (long long)m_camera.cell.x,
        (long long)m_camera.cell.y,
        (long long)m_camera.cell.z
    );
    if (!m_error.empty())
    {
        ImGui::TextWrapped("%s", m_error.c_str());
    }
    ImGui::End();
}
} // namespace ofg

namespace ofg {
void TerrainLaboratory::boundsOverlay(math::Vec4 rectangle)
{
    if (!m_bounds)
    {
        return;
    }
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect({rectangle.x, rectangle.y}, {rectangle.z, rectangle.w});
    for (const auto& entry : m_snapshot)
    {
        const auto origin = terrain::relativeMinimum(entry.address, m_camera, m_settings.rootWidth);
        const float width = float(m_settings.rootWidth) / float(1u << entry.address.depth);
        std::array<math::Vec4, 8> corners;
        for (unsigned i = 0; i < 8; ++i)
        {
            corners[i] = math::mul(
                m_clipFromWorld,
                math::Vec4{
                    float(origin[0]) + (i & 1 ? width : 0),
                    float(origin[1]) + (i & 2 ? width : 0),
                    float(origin[2]) + (i & 4 ? width : 0),
                    1
                }
            );
        }
        const auto color = ImGui::ColorConvertFloat4ToU32(
            ImColor::HSV(entry.address.depth / float(m_settings.maximumDepth), .8f, 1.f)
        );
        for (unsigned i = 0; i < 8; ++i)
        {
            for (unsigned bit : {1u, 2u, 4u})
            {
                if (i & bit)
                {
                    continue;
                }
                const auto a = corners[i], b = corners[i | bit];
                if (a.w <= .001f || b.w <= .001f || a.z < 0 || b.z < 0)
                {
                    continue;
                }
                const ImVec2 start{
                    rectangle.x + (a.x / a.w + 1) * .5f * (rectangle.z - rectangle.x),
                    rectangle.y + (1 - a.y / a.w) * .5f * (rectangle.w - rectangle.y)
                };
                const ImVec2 end{
                    rectangle.x + (b.x / b.w + 1) * .5f * (rectangle.z - rectangle.x),
                    rectangle.y + (1 - b.y / b.w) * .5f * (rectangle.w - rectangle.y)
                };
                draw->AddLine(start, end, color);
            }
        }
    }
    draw->PopClipRect();
}
} // namespace ofg
