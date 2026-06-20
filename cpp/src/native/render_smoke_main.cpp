// Entry point for the native C++ Dawn render-smoke executable.
//
// The executable is intentionally thin: the Node wrapper passes the shared
// smoke contract as command-line arguments, and render_smoke.cpp owns all Dawn
// setup, rendering, artifact writing, and threshold validation.
#include "ofg/native/render_smoke.hpp"

#include <exception>
#include <iostream>

// Parses the smoke contract, runs the render smoke, and reports one-line failures.
int main(int argc, char** argv) {
  try {
    const ofg::native::RenderSmokeOptions options =
      ofg::native::parse_render_smoke_args(argc, argv);
    ofg::native::run_render_smoke(options);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native C++ render smoke failed: " << error.what() << "\n";
    return 1;
  }
}
