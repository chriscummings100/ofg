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
- Reloads the already-running page, waits for terrain to settle again, captures a
  refreshed first-person screenshot, and fails on black or blank refresh frames.
- Presses `C` and verifies the camera mode changes from `FIRST` to `FLY`.
- Returns to first-person, moves the player across terrain chunk columns through
  the debug hook, verifies streamed chunk keys, and captures a streamed view.

## Useful Environment Variables

- `OFG_SMOKE_PORT`: preferred local port. Defaults to `5174`.
- `OFG_BROWSER_PATH`: explicit Chromium-based browser executable path.
- `OFG_SMOKE_HEADED=1`: launch a visible browser window for debugging.

## What This Catches

- WebGPU unavailable in the selected browser.
- Blank, transparent, or nearly solid-color frames.
- Refresh-only black screens, including stale browser-cache or teardown/restart
  regressions that do not appear on a fresh first load.
- Regressions where the first-person scene is hidden by the player marker.
- HUD mode not matching expected camera state.
- Keyboard toggle regressions for the debug fly camera.
- Basic terrain chunk streaming regressions where moving the player no longer
  causes the expected chunk column to load.

## Current Limit

This is a smoke test, not a full visual diff. It verifies that the page renders
meaningful pixels and that core interaction works. Future browser tests can add
targeted checks for pointer-lock look controls, real keyboard movement, terrain
edit visibility, and object placement.
