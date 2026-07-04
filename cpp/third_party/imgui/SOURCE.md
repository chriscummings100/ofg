# Dear ImGui Source

This directory vendors the Dear ImGui files needed for OFG's C++ debug renderer.

- Upstream: https://github.com/ocornut/imgui
- Version: `v1.92.8`
- Commit: `8936b58fe26e8c3da834b8f60b06511d537b4c63`
- Imported: 2026-07-04
- Local changes: none

Imported files are limited to the core Dear ImGui implementation, public/internal headers, bundled stb headers, the license, and the WebGPU renderer backend:

- `imgui.cpp`
- `imgui.h`
- `imgui_draw.cpp`
- `imgui_internal.h`
- `imgui_tables.cpp`
- `imgui_widgets.cpp`
- `imconfig.h`
- `imstb_rectpack.h`
- `imstb_textedit.h`
- `imstb_truetype.h`
- `LICENSE.txt`
- `backends/imgui_impl_wgpu.cpp`
- `backends/imgui_impl_wgpu.h`

Excluded files include `imgui_demo.cpp`, examples, docking branch files, GLFW/SDL backends, renderer backends other than WebGPU, and optional FreeType integration.

To refresh this vendor snapshot, import the same file list from the chosen upstream tag or commit and update this note with the new version, commit, import date, and any local changes.
