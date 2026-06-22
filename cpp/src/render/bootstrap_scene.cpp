// Deterministic bootstrap triangle scene data shared by C++ render paths.
#include "ofg/render/bootstrap_scene.hpp"

#include <type_traits>

namespace ofg {
namespace {

// Compile-time layout checks keep C++ vertex data compatible with WebGPU.
static_assert(std::is_standard_layout_v<BootstrapVertex>);
static_assert(std::is_trivially_copyable_v<BootstrapVertex>);
static_assert(sizeof(BootstrapVertex) == 20);

// Stores the bootstrap vertices in a stable layout verified by doctests.
constexpr std::array<BootstrapVertex, 3> _bootstrap_vertices{
    {BootstrapVertex{std::array<float, 2>{-0.72F, -0.58F}, std::array<float, 3>{1.0F, 0.05F, 0.04F}},
        BootstrapVertex{std::array<float, 2>{0.72F, -0.58F}, std::array<float, 3>{0.05F, 0.95F, 0.18F}},
        BootstrapVertex{std::array<float, 2>{0.0F, 0.7F}, std::array<float, 3>{0.08F, 0.28F, 1.0F}}}};

} // namespace

// Returns the deterministic RGB triangle used by browser and native smokes.
const std::array<BootstrapVertex, 3>& bootstrap_vertices() noexcept {
    return _bootstrap_vertices;
}

} // namespace ofg
