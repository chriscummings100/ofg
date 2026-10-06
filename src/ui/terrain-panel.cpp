// Terrain inspection controls; this translation unit keeps ImGui out of the rendering library.
#include "lab/terrain-laboratory.h"
#include <imgui.h>
namespace ofg {
void TerrainLaboratory::panel()
{
    ImGui::Begin("Terrain streaming");
    ImGui::Checkbox("Freeze observer", &m_freeze);
    ImGui::Checkbox("LOD colors", &m_lodColors);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Cell widths: 1024, 512, 256, 128, 64, 32, 16 metres (coarse to fine)");
    }
    ImGui::Checkbox("Node bounds", &m_bounds);
    ImGui::Checkbox("Hold next job", &m_holdNext);
    ImGui::Checkbox("Fail next job", &m_failNext);
    if (ImGui::Button("Release held jobs"))
    {
        m_workers.releaseHeld();
    }
    ImGui::SameLine();
    if (ImGui::Button("Retry failures"))
    {
        for (auto key : m_failures)
        {
            m_stream.retry(key);
        }
        m_failures.clear();
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
    if (ImGui::Button("Reseed"))
    {
        ++m_generator.seed;
        m_stream.reset();
        m_roots.clear();
    }
    const auto d = diagnostics();
    ImGui::TextWrapped("%.1f fps; terrain update %.2f ms", double(ImGui::GetIO().Framerate), m_updateMilliseconds);
    ImGui::TextWrapped(
        "Finest surface cell %u m; %zu leaves want refinement%s",
        1024u >> d.deepestSurfaceDepth,
        d.unresolvedRefinements,
        d.planningIdle ? "; planner idle" : ""
    );
    if (m_admissionBlocked)
    {
        ImGui::TextUnformatted("Waiting for root metadata capacity");
    }
    ImGui::TextWrapped("Roots %zu admitted / %zu loading; cut %zu", d.admittedRoots, d.loadingRoots, d.selected);
    ImGui::TextWrapped("Nodes %zu; jobs %zu; failed %zu", d.nodes, d.jobs, d.failed);
    ImGui::TextWrapped(
        "CPU payload %.1f / 256 MiB",
        double(d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes) / (1 << 20)
    );
    ImGui::TextUnformatted("Scratch/transport allowance: 128 MiB");
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
        const auto origin = terrain::relativeMinimum(entry.address, m_camera, 1024);
        const float width = 1024.f / float(1u << entry.address.depth);
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
        const auto color = ImGui::ColorConvertFloat4ToU32(ImColor::HSV(entry.address.depth / 6.f, .8f, 1.f));
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
