// Concrete laboratory panels; only docking setup/hit-testing use ImGui internals at the pinned revision.
#include "ui/workspace.h"
#include "ui/render-settings-panel.h"
#include "lab/render-settings.h"
#include "core/engine-error.h"
#include "game.h"
#include "lab/terrain-laboratory.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <numbers>
#include <sstream>

namespace ofg {
Workspace::Workspace(rhi::IDevice* device, rhi::ICommandQueue* queue)
{
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    try
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.Fonts->AddFontDefault();
        m_renderer = std::make_unique<ImGuiRenderer>(device, queue);
    } catch (...)
    {
        ImGui::DestroyContext(m_context);
        throw;
    }
}

Workspace::~Workspace()
{
    ImGui::SetCurrentContext(m_context);
    m_renderer.reset();
    ImGui::DestroyContext(m_context);
}

void Workspace::loadLayout(const std::string& settings)
{
    if (settings.empty())
    {
        return;
    }
    std::istringstream input(settings);
    std::string magic;
    int scene = -1, hierarchy = -1, render = -1;
    input >> magic >> scene >> hierarchy >> render;
    if (magic != "OFG-UI-1" || scene < 0 || scene > 1 || hierarchy < 0 || hierarchy > 1 || render < 0 || render > 1)
    {
        std::fprintf(stderr, "Ignoring malformed OFG UI layout.\n");
        return;
    }
    m_showScene = scene != 0;
    m_showHierarchy = hierarchy != 0;
    m_showSettings = render != 0;
    // The optional fourth visibility field preserves compatibility with saved OFG-UI-1 layouts.
    std::string remainder;
    std::getline(input, remainder);
    std::istringstream flags(remainder);
    int animation = 1;
    m_showAnimation = true;
    if ((flags >> animation) && (animation == 0 || animation == 1))
    {
        m_showAnimation = animation != 0;
    }
    auto newline = settings.find('\n');
    if (newline != std::string::npos)
    {
        ImGui::LoadIniSettingsFromMemory(settings.c_str() + newline + 1);
    }
}

std::string Workspace::saveLayout()
{
    std::string header = "OFG-UI-1 " + std::to_string(m_showScene) + " " + std::to_string(m_showHierarchy) + " " +
                         std::to_string(m_showSettings) + " " + std::to_string(m_showAnimation) + "\n";
    header += ImGui::SaveIniSettingsToMemory();
    ImGui::GetIO().WantSaveIniSettings = false;
    return header;
}

void Workspace::menu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Window"))
        {
            bool changed = ImGui::MenuItem("Scene", nullptr, &m_showScene);
            changed |= ImGui::MenuItem("Scene Hierarchy", nullptr, &m_showHierarchy);
            changed |= ImGui::MenuItem("Render Settings", nullptr, &m_showSettings);
            changed |= ImGui::MenuItem("Animation", nullptr, &m_showAnimation);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                m_resetLayout = true;
                m_showScene = m_showHierarchy = m_showSettings = true;
                m_showAnimation = true;
                changed = true;
            }
            if (changed)
            {
                ImGui::GetIO().WantSaveIniSettings = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("Camera Controls", nullptr, &m_showHelp);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    if (m_showHelp)
    {
        if (ImGui::Begin("Camera Controls", &m_showHelp, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(
                "Hold right mouse over the scene to fly.\nWASD: move   Q/E: down/up   Shift: faster\nR: overview   F: "
                "close-up   0-4: debug view\nRelease right mouse or press Escape to return to the UI."
            );
        }
        ImGui::End();
    }
}

void Workspace::buildDockspace()
{
    auto viewport = ImGui::GetMainViewport();
    const auto id = ImGui::GetID("LaboratoryDockspace");
    if (m_resetLayout || !ImGui::DockBuilderGetNode(id))
    {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        auto centre = id;
        auto left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.23f, nullptr, &centre);
        auto right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.30f, nullptr, &centre);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
        ImGui::DockBuilderDockWindow("Render Settings", right);
        ImGui::DockBuilderDockWindow("Animation", right);
        ImGui::DockBuilderDockWindow("Scene", centre);
        ImGui::DockBuilderFinish(id);
        m_resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(id, viewport);
}

void Workspace::entityRow(Entity& entity)
{
    ImGui::PushID(int(entity.id()));
    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (&entity == m_selected.get())
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!entity.firstChild())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (!entity.parent())
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    const auto name = entity.name().empty() ? "Entity " + std::to_string(entity.id()) : entity.name();
    const char* component = entity.camera() ? " [Camera]" : entity.meshRenderer() ? " [Mesh]" : "";
    bool open = ImGui::TreeNodeEx("entity", flags, "%s%s", name.c_str(), component);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_selected = &entity;
    }
    if (open)
    {
        for (auto child = entity.firstChild(); child; child = child->nextSibling())
        {
            entityRow(*child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Workspace::hierarchy(Scene& scene)
{
    if (!m_showHierarchy)
    {
        return;
    }
    if (ImGui::Begin("Scene Hierarchy", &m_showHierarchy))
    {
        ImGui::Text("%zu entities", scene.entityCount());
        if (ImGui::BeginChild("Tree", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.60f), ImGuiChildFlags_Borders))
        {
            ImGui::PushID(int(m_sceneEpoch));
            if (auto root = scene.getRoot())
            {
                entityRow(*root);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (auto entity = m_selected.get())
        {
            ImGui::SeparatorText("Selection");
            ImGui::TextWrapped("%s", entity->name().empty() ? "Unnamed entity" : entity->name().c_str());
            ImGui::Text("ID: %u", entity->id());
            const auto& t = entity->localTransform();
            ImGui::Text("Local position (m)\n%.3f, %.3f, %.3f", t.position.x, t.position.y, t.position.z);
            ImGui::Text(
                "Local rotation (quaternion)\n%.3f, %.3f, %.3f, %.3f",
                t.rotation.x,
                t.rotation.y,
                t.rotation.z,
                t.rotation.w
            );
            ImGui::Text("Local scale\n%.3f, %.3f, %.3f", t.scale.x, t.scale.y, t.scale.z);
            auto p = entity->worldTransform()[3];
            ImGui::Text("World position (m)\n%.3f, %.3f, %.3f", p.x, p.y, p.z);
            if (entity->camera())
            {
                ImGui::TextUnformatted(entity->camera() == scene.activeCamera() ? "Camera (active)" : "Camera");
            }
            if (auto renderer = entity->meshRenderer())
            {
                ImGui::Text("MeshRenderer: %zu submeshes", renderer->mesh() ? renderer->mesh()->subMeshes().size() : 0);
            }
        }
        else
        {
            ImGui::TextWrapped("Select an entity to inspect its transform and components.");
        }
    }
    ImGui::End();
}

void Workspace::renderSettings(Scene& scene)
{
    if (!m_showSettings)
    {
        return;
    }
    if (ImGui::Begin("Render Settings", &m_showSettings))
    {
        if (!scene.lighting.hdr)
        {
            ImGui::TextWrapped("This diagnostic scene does not use PBR lighting or tone mapping.");
        }
        ImGui::BeginDisabled(!scene.lighting.hdr);
        auto draft = scene.lighting;
        bool changed = drawLightingControls(draft);
        if (changed)
        {
            m_editError = applyLightingEdit(scene.lighting, draft) ? "" : "Invalid value; previous settings retained.";
        }
        if (ImGui::Button("Reset Render Settings"))
        {
            scene.lighting = m_initialLighting;
            m_editError.clear();
        }
        ImGui::EndDisabled();
        if (!m_editError.empty())
        {
            ImGui::TextWrapped("%s", m_editError.c_str());
        }
    }
    ImGui::End();
}

void Workspace::scenePanel()
{
    m_sceneVisible = m_sceneFocused = false;
    m_sceneWindow = nullptr;
    m_sceneRectangle = {};
    if (!m_showScene)
    {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene", &m_showScene, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        auto extent = ImGui::GetContentRegionAvail();
        auto scale = ImGui::GetIO().DisplayFramebufferScale;
        const auto width = int(extent.x * scale.x), height = int(extent.y * scale.y);
        if (width > 0 && height > 0)
        {
            if (!m_sceneTarget || m_sceneTarget->getDesc().size.width != uint32_t(width) ||
                m_sceneTarget->getDesc().size.height != uint32_t(height))
            {
                m_sceneTarget = m_renderer->createTarget(width, height);
            }
            auto p = ImGui::GetCursorScreenPos();
            ImGui::Image(ImTextureRef(ImGuiRenderer::textureId(m_sceneTarget)), extent);
            m_sceneRectangle = {p.x, p.y, p.x + extent.x, p.y + extent.y};
            m_sceneVisible = true;
            m_sceneWindow = ImGui::GetCurrentWindow();
            m_sceneFocused = ImGui::IsWindowFocused();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Workspace::begin(Scene& scene, float deltaSeconds, float styleScale)
{
    ImGui::SetCurrentContext(m_context);
    auto& io = ImGui::GetIO();
    io.DeltaTime = std::max(deltaSeconds, 0.000001f);
    if (styleScale != m_styleScale)
    {
        ImGui::GetStyle() = ImGuiStyle{};
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(styleScale);
        ImGui::GetStyle().FontScaleDpi = styleScale;
        m_styleScale = styleScale;
    }
    if (!m_sceneRoot || m_sceneRoot.get() != scene.getRoot())
    {
        m_sceneRoot = scene.getRoot();
        m_selected.reset();
        m_animator.reset();
        m_animationError.clear();
        m_initialLighting = scene.lighting;
        m_editError.clear();
        ++m_sceneEpoch;
    }
    ImGui::NewFrame();
    const int visibility =
        int(m_showScene) | (int(m_showHierarchy) << 1) | (int(m_showSettings) << 2) | (int(m_showAnimation) << 3);
    menu();
    buildDockspace();
    hierarchy(scene);
    renderSettings(scene);
    animationPanel(scene);
    scenePanel();
    if (Game::terrain())
    {
        Game::terrain()->panel();
        if (m_sceneVisible)
        {
            Game::terrain()->boundsOverlay(
                {m_sceneRectangle.x, m_sceneRectangle.y, m_sceneRectangle.z, m_sceneRectangle.w}
            );
        }
    }
    if (visibility !=
        (int(m_showScene) | (int(m_showHierarchy) << 1) | (int(m_showSettings) << 2) | (int(m_showAnimation) << 3)))
    {
        io.WantSaveIniSettings = true;
    }
}

bool Workspace::canCaptureAt(float x, float y) const
{
    if (!m_sceneVisible || x < m_sceneRectangle.x || y < m_sceneRectangle.y || x >= m_sceneRectangle.z ||
        y >= m_sceneRectangle.w ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        m_context->MovingWindow)
    {
        return false;
    }
    ImGuiWindow* hovered = nullptr;
    ImGuiWindow* underneath = nullptr;
    ImGui::FindHoveredWindowEx({x, y}, false, &hovered, &underneath);
    return hovered == m_sceneWindow && !ImGui::IsAnyItemActive();
}

bool Workspace::cameraKeyboardAllowed() const
{
    return m_sceneVisible && m_sceneFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
           !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

rhi::ITexture* Workspace::finish()
{
    ImGui::Render();
    const auto& io = ImGui::GetIO();
    uint32_t width = uint32_t(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    uint32_t height = uint32_t(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (!width || !height)
    {
        return nullptr;
    }
    if (!m_outputTarget || m_outputTarget->getDesc().size.width != width ||
        m_outputTarget->getDesc().size.height != height)
    {
        m_outputTarget = m_renderer->createTarget(width, height);
    }
    m_renderer->render(*ImGui::GetDrawData(), m_outputTarget);
    return m_outputTarget;
}
} // namespace ofg
