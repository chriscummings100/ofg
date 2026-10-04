# Laboratory workspace

The default native and browser applications provide a Dear ImGui docking workspace. The Window menu opens Scene, Scene Hierarchy and Render Settings, or resets their layout. Panels can dock, tab, float inside the application, resize and close. Separate OS windows are intentionally disabled.

Scene contains the existing PBR laboratory. Hold the right mouse button over its image and use WASD/QE/Shift to fly; release, Escape or focus loss returns to the UI. R/F and 0�4 operate when the Scene panel owns keyboard focus. Text entry in another panel must never reset the camera or change debug view. Escape does not close the workspace. Camera navigation is enabled for the PBR fixture; the original cube fixture retains its fixed inspection camera.

Scene Hierarchy shows optional entity names, IDs, parent/child ordering and component indicators. Selection displays local position, quaternion rotation, scale, world position and component summary. Selection observes the entity through Ptr and becomes null when the scene clears or is replaced. Names are presentation data, not unique identifiers. Editing entities and scene picking are outside this milestone.

Render Settings edits existing Scene::lighting: exposure multiplier, PBR Neutral/linear diagnostic output, debug views, environment intensity/rotation and existing punctual lights. Lengths are metres, angles displayed in degrees, directional intensity lux, other intensity candela, and light colours linear. Invalid edits preserve previous settings. Reset Render Settings restores the fixture snapshot without changing the camera or layout. The non-PBR cube diagnostic disables these controls.

## Ownership and frame order

Each host explicitly owns one Workspace and its platform input backend. Workspace owns its ImGui context, panel state, selection, initial lighting snapshot, scene image and composition image. ImGuiRenderer directly owns RHI pipelines, sampler and atlas images. ofg-core has no ImGui or RHI dependency. Native uses upstream imgui_impl_glfw; browser uses the existing canvas and Emscripten event callbacks.

Frame order is events, UI layout/settings, eligible camera input, Game::frame into the scene image, UI composition, then presentation. Hidden/zero-sized Scene panels still run update-only Game frames. Scene allocation matches panel physical pixels, so camera aspect follows docking. Both scene and composition images use display-linear RGBA16F; UI vertex colours are decoded, font atlas RGB is white, and final output encodes exactly once. Browser acquires the canvas only after scene/UI work finishes and uses the resource-only presentation pass to avoid an Asyncify yield while a canvas texture is live.

Frame vertex/index buffers and dynamic atlas snapshots are immutable after upload. Submitted RHI commands retain resources; UI code does not cycle through a guessed number of frames or overwrite a live upload. Atlas updates currently upload a complete atlas, intentionally favouring simple ownership over partial updates. No performance/residency optimization claim is made.

The dependency is Dear ImGui v1.92.9b-docking, commit b48d1afbe8ee8b238e2961dc363a949dd7304e23. SGL's UI integration at bdbc9f3f809c5f6eda7e61e6eecae787db3f9620 informed the separation and draw-data approach; OFG's renderer is independently written, and no SGL runtime or code is imported. Upstream ImGui license remains in its submodule.

## Persistence and diagnostics

Only panel layout/visibility is persisted: native %LOCALAPPDATA%/OFG/workspace.ini on clean exit, browser localStorage key ofg.workspace.v1 when ImGui requests a save. Browser layouts are scoped to the current origin. Missing settings use defaults; unavailable storage reports a diagnostic and leaves the session usable. Render values and selection are not persisted. Window > Reset Layout restores all three panels.

Use --no-ui or ?ui=0 for the original full-canvas scene/PBR diagnostics. --checkerboard and ?demo=checkerboard continue to bypass UI. Build and test commands are in DEVELOPING.md. The implementation plan and actual validation evidence are tracked in [the workspace ExecPlan](archived/imgui-workspace.md).
