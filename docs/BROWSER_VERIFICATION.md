# Browser Verification

Use browser smoke tests whenever a change affects rendering, input, camera behavior,
or UI state.

## Command

```powershell
npm run smoke:browser
```

The command:

- Builds the TypeScript app.
- Starts a temporary local dev server on an available port.
- Launches the installed system Chrome or Edge through Playwright Core.
- Saves screenshots under `artifacts/browser-smoke/`.
- Reads HUD state from the page.
- Samples screenshot pixels to catch blank or solid-fill frames.
- Presses `C` and verifies the camera mode changes from `FIRST` to `FLY`.

## Useful Environment Variables

- `OFG_SMOKE_PORT`: preferred local port. Defaults to `5174`.
- `OFG_BROWSER_PATH`: explicit Chromium-based browser executable path.
- `OFG_SMOKE_HEADED=1`: launch a visible browser window for debugging.

## What This Catches

- WebGPU unavailable in the selected browser.
- Blank, transparent, or nearly solid-color frames.
- Regressions where the first-person scene is hidden by the player marker.
- HUD mode not matching expected camera state.
- Keyboard toggle regressions for the debug fly camera.

## Current Limit

This is a smoke test, not a full visual diff. It verifies that the page renders
meaningful pixels and that core interaction works. Future browser tests can add
targeted checks for pointer-lock look controls, movement, chunk streaming, and
object placement.
