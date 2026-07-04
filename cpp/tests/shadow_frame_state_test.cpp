// Doctest coverage for the opaque shadow frame uniform contract.
#include "doctest.h"

#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_frame_state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace {

// Produces a non-null fake texture view handle that is never dereferenced.
WGPUTextureView fake_texture_view(std::uintptr_t value) noexcept {
    return reinterpret_cast<WGPUTextureView>(value);
}

// Produces a non-null fake sampler handle that is never dereferenced.
WGPUSampler fake_sampler(std::uintptr_t value) noexcept {
    return reinterpret_cast<WGPUSampler>(value);
}

// Builds deterministic cascades for uniform packing tests.
ofg::ShadowCascadeSet make_test_cascades(ofg::ShadowSettings settings) {
    const ofg::CameraProperties camera = ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 2.0f, -6.0f),
        ofg::math::vec3(0.0f, 1.0f, 8.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        1.04719755f,
        1.4f,
        0.1f,
        80.0f);

    std::string error;
    const std::optional<ofg::math::Vec3> light_direction =
        ofg::math::normalize(ofg::math::vec3(-0.35f, -1.0f, -0.25f), error);
    REQUIRE_MESSAGE(light_direction.has_value(), error);
    return ofg::build_shadow_cascades(camera, *light_direction, settings);
}

} // namespace

// Verifies disabled shadow state keeps shader sampling neutral.
TEST_CASE("shadow frame state packs disabled neutral uniforms") {
    ofg::ShadowSettings settings;
    settings.m_pcf_mode = ofg::ShadowPcfMode::NineTap;
    const ofg::ShadowFrameState state = ofg::make_disabled_shadow_frame_state(settings);
    CHECK(ofg::shadow_frame_state_has_live_sampling(state) == false);

    const ofg::ShadowFrameUniforms uniforms = ofg::pack_shadow_frame_uniforms(state);
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_matrix_offset(0)] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_matrix_offset(1)] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_matrix_offset(2)] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_cascade_end_offset() + 0U] == doctest::Approx(12.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_texel_size_offset() + 3U] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 0U] == doctest::Approx(0.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 1U] == doctest::Approx(0.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options2_offset() + 0U] == doctest::Approx(2.0f));
}

// Verifies live shadow state publishes cascade distances, texels, intensity, and PCF controls.
TEST_CASE("shadow frame state packs live cascade uniforms") {
    ofg::ShadowSettings settings;
    settings.m_map_size = 256;
    settings.m_intensity = 0.5f;
    settings.m_receiver_depth_bias = 0.003f;
    settings.m_normal_bias = 0.12f;
    settings.m_pcf_mode = ofg::ShadowPcfMode::FiveTap;
    settings.m_pcf_radius_texels = 2.0f;
    const ofg::ShadowCascadeSet cascades = make_test_cascades(settings);
    const ofg::ShadowFrameState state = ofg::make_shadow_frame_state(
        cascades, settings, fake_texture_view(17), fake_sampler(19), settings.m_map_size, 23);
    CHECK(ofg::shadow_frame_state_has_live_sampling(state));

    const ofg::ShadowFrameUniforms uniforms = ofg::pack_shadow_frame_uniforms(state);
    const std::array<float, 16> cascade_matrix = ofg::math::pack_mat4(cascades.m_cascades[0].m_clip_from_world);
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_matrix_offset(0) + 0U] == doctest::Approx(cascade_matrix[0]));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_cascade_end_offset() + 0U] ==
          doctest::Approx(cascades.m_cascades[0].m_far_distance));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_cascade_end_offset() + 2U] ==
          doctest::Approx(cascades.m_cascades[2].m_far_distance));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_blend_width_offset() + 1U] ==
          doctest::Approx(settings.m_cascade_blend_widths[1]));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_texel_size_offset() + 0U] ==
          doctest::Approx(cascades.m_cascades[0].m_texel_world_size));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_texel_size_offset() + 3U] == doctest::Approx(1.0f / 256.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 0U] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 1U] ==
          doctest::Approx(cascades.m_effective_intensity));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 2U] ==
          doctest::Approx(settings.m_receiver_depth_bias));
    CHECK(
        uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 3U] == doctest::Approx(settings.m_normal_bias));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options2_offset() + 0U] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options2_offset() + 1U] ==
          doctest::Approx(settings.m_pcf_radius_texels));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options2_offset() + 2U] == doctest::Approx(256.0f));
}

// Verifies missing texture handles keep the shader neutral even when cascades exist.
TEST_CASE("shadow frame state disables sampling when live handles are incomplete") {
    ofg::ShadowSettings settings;
    const ofg::ShadowCascadeSet cascades = make_test_cascades(settings);
    const ofg::ShadowFrameState state =
        ofg::make_shadow_frame_state(cascades, settings, nullptr, fake_sampler(21), settings.m_map_size, 1);

    CHECK(ofg::shadow_frame_state_has_live_sampling(state) == false);
    const ofg::ShadowFrameUniforms uniforms = ofg::pack_shadow_frame_uniforms(state);
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 0U] == doctest::Approx(0.0f));
    CHECK(uniforms.m_values[ofg::shadow_frame_uniform_options_offset() + 1U] == doctest::Approx(0.0f));
}
