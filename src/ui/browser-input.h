// Emscripten platform input for the existing canvas host; pointer lock begins only in a scene RMB gesture.
#pragma once
#include "ui/workspace.h"
#include "lab/fly-camera.h"
#include <emscripten/html5.h>

namespace ofg {
class BrowserInput
{
public:
    // Installs canvas/document callbacks; workspace must outlive this platform backend.
    explicit BrowserInput(Workspace& workspace);
    // Removes callbacks and releases pointer lock before the UI context is destroyed.
    ~BrowserInput();
    // Updates the cursor and consumes eligible camera input after UI layout is built.
    FlyCameraInput cameraInput(Scene& scene);
    // Clears camera state and releases any pending/active capture.
    void release();

private:
    // Queues pointer input and synchronously requests lock only during an eligible right-button gesture.
    static EM_BOOL mouse(int type, const EmscriptenMouseEvent* event, void* context);
    // Queues scroll in ImGui's wheel units, preventing page scrolling over the canvas.
    static EM_BOOL wheel(int, const EmscriptenWheelEvent* event, void* context);
    // Queues keys/modifiers and printable UTF-8 characters, reserving browser shortcuts.
    static EM_BOOL keyboard(int type, const EmscriptenKeyboardEvent* event, void* context);
    // Clears held inputs and camera capture on page focus loss.
    static EM_BOOL blur(int, const EmscriptenFocusEvent*, void* context);
    // Acknowledges asynchronous lock changes; a release before acquisition immediately exits lock.
    static EM_BOOL pointerLock(int, const EmscriptenPointerlockChangeEvent* event, void* context);
    Workspace& m_workspace;
    bool m_rightHeld = false, m_captured = false;
    math::Vec2 m_look{};
};
} // namespace ofg
