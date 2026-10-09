// One explicitly owned laboratory UI context, its panels, selection and offscreen composition targets.
#pragma once
#include "ui/imgui-renderer.h"
#include "scene/scene.h"
#include <memory>
#include <string>

struct ImGuiWindow;
namespace ofg {
class Workspace
{
public:
    // Creates the sole host UI context. Platform backend initialization follows construction.
    Workspace(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Requires platform callbacks/backend to be disconnected first; native host drains GPU submissions.
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    // Starts UI and builds panels. Host has supplied current input/display metrics in ImGuiIO first.
    void begin(Scene& scene, float deltaSeconds, float styleScale = 1);
    // Finalizes draw data and submits composition after Game rendered into sceneTarget().
    rhi::ITexture* finish();
    // Returns null for hidden/zero-area scene panels; Game must still advance update-only frames.
    rhi::ITexture* sceneTarget() const { return m_sceneVisible ? m_sceneTarget.get() : nullptr; }
    // Tests current logical coordinates against scene content and topmost UI, including menus/floating panels.
    bool canCaptureAt(float x, float y) const;
    // Allows camera actions only when scene is focused and no text/item/menu interaction owns the keyboard.
    bool cameraKeyboardAllowed() const;
    // Restores layout/visibility from a host-owned string; malformed headers leave defaults intact.
    void loadLayout(const std::string& settings);
    // Serializes layout/visibility only and acknowledges ImGui's pending save request.
    std::string saveLayout();
    // Returns the scene image rectangle in logical host coordinates, for diagnostics/input integration.
    ImVec4 sceneRectangle() const { return m_sceneRectangle; }
    // Returns current selection for read-only diagnostics.
    Entity* selection() const { return m_selected.get(); }

private:
    // Creates the initial split only on first launch or explicit layout reset.
    void buildDockspace();
    // Builds window toggles, reset layout and camera help.
    void menu();
    // Draws stable-ID entity rows recursively in parent/child order.
    void entityRow(Entity& entity);
    // Draws the tree and selected entity's read-only details.
    void hierarchy(Scene& scene);
    // Edits existing lighting/output parameters, committing only valid complete drafts.
    void renderSettings(Scene& scene);
    // Selects an animator and edits its playback transport; scrub changes pause before sampling.
    void animationPanel(Scene& scene);
    // Creates/resizes scene storage and adds the image to the dockable scene panel.
    void scenePanel();
    ImGuiContext* m_context{};
    std::unique_ptr<ImGuiRenderer> m_renderer;
    rhi::ComPtr<rhi::ITexture> m_sceneTarget, m_outputTarget;
    Ptr<Entity> m_sceneRoot, m_selected;
    Ptr<Animator> m_animator;
    Lighting m_initialLighting;
    unsigned m_sceneEpoch = 0;
    bool m_showScene = true, m_showHierarchy = true, m_showSettings = true, m_showHelp = false;
    bool m_resetLayout = false, m_sceneVisible = false, m_sceneFocused = false;
    bool m_showAnimation = true;
    bool m_terrainDocked = false;
    ImVec4 m_sceneRectangle{};
    ImGuiWindow* m_sceneWindow{};
    float m_styleScale = 0;
    std::string m_editError;
    std::string m_animationError;
};
} // namespace ofg
