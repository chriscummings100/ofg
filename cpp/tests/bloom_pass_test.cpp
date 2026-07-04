// Doctest coverage for BloomPass GPU state, TempBuffer use, and tone-map composition.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/render/bloom_pass.hpp"
#include "ofg/render/scene_color_target.hpp"
#include "ofg/render/temp_buffer.hpp"
#include "ofg/render/tone_map_pass.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

// Cleans the static TempBuffer singleton after each test.
struct TempBufferReset {
    // Releases and destroys the TempBuffer singleton on scope exit.
    ~TempBufferReset() {
        while (!ofg::TempBuffer::release()) {}
        ofg::TempBuffer::destroy();
    }
};

struct ScopedTexture {
    WGPUTexture m_value{nullptr};

    ScopedTexture() = default;
    explicit ScopedTexture(WGPUTexture value) : m_value(value) {}
    ScopedTexture(const ScopedTexture&) = delete;
    ScopedTexture& operator=(const ScopedTexture&) = delete;
    ScopedTexture(ScopedTexture&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedTexture& operator=(ScopedTexture&& other) noexcept = delete;

    // Releases the temporary render texture.
    ~ScopedTexture() {
        if (m_value != nullptr) {
            wgpuTextureRelease(m_value);
        }
    }
};

struct ScopedTextureView {
    WGPUTextureView m_value{nullptr};

    ScopedTextureView() = default;
    explicit ScopedTextureView(WGPUTextureView value) : m_value(value) {}
    ScopedTextureView(const ScopedTextureView&) = delete;
    ScopedTextureView& operator=(const ScopedTextureView&) = delete;
    ScopedTextureView(ScopedTextureView&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedTextureView& operator=(ScopedTextureView&& other) noexcept = delete;

    // Releases the temporary render texture view.
    ~ScopedTextureView() {
        if (m_value != nullptr) {
            wgpuTextureViewRelease(m_value);
        }
    }
};

struct ScopedCommandEncoder {
    WGPUCommandEncoder m_value{nullptr};

    ScopedCommandEncoder() = default;
    explicit ScopedCommandEncoder(WGPUCommandEncoder value) : m_value(value) {}
    ScopedCommandEncoder(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder& operator=(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder(ScopedCommandEncoder&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedCommandEncoder& operator=(ScopedCommandEncoder&& other) noexcept = delete;

    // Releases the encoder when a test exits before finish.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

struct ScopedCommandBuffer {
    WGPUCommandBuffer m_value{nullptr};

    ScopedCommandBuffer() = default;
    explicit ScopedCommandBuffer(WGPUCommandBuffer value) : m_value(value) {}
    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer(ScopedCommandBuffer&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedCommandBuffer& operator=(ScopedCommandBuffer&& other) noexcept = delete;

    // Releases the finished command buffer.
    ~ScopedCommandBuffer() {
        if (m_value != nullptr) {
            wgpuCommandBufferRelease(m_value);
        }
    }
};

struct ScopedBuffer {
    WGPUBuffer m_value{nullptr};

    ScopedBuffer() = default;
    explicit ScopedBuffer(WGPUBuffer value) : m_value(value) {}
    ScopedBuffer(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(const ScopedBuffer&) = delete;
    ScopedBuffer(ScopedBuffer&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedBuffer& operator=(ScopedBuffer&& other) noexcept = delete;

    // Releases the temporary readback buffer.
    ~ScopedBuffer() {
        if (m_value != nullptr) {
            wgpuBufferRelease(m_value);
        }
    }
};

struct MapRequest {
    WGPUMapAsyncStatus m_status{WGPUMapAsyncStatus_Error};
    std::string m_message;
};

// Stores the mapAsync result after Dawn finishes readback synchronization.
void handle_map_request(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<MapRequest*>(userdata1);
    request->m_status = status;
    request->m_message.assign(message.data, message.length);
}

// Aligns row pitch to WebGPU texture-to-buffer copy requirements.
std::uint32_t align_to(std::uint32_t value, std::uint32_t alignment) {
    return ((value + alignment - 1U) / alignment) * alignment;
}

// Converts a finite positive float to a half-float bit pattern for RGBA16Float uploads.
std::uint16_t float_to_half_bits(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23U) & 0xffU) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7fffffU;
    if (exponent <= 0) {
        return static_cast<std::uint16_t>(sign);
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10U) | (mantissa >> 13U));
}

// Creates a tiny output texture for tone-map composition tests.
ScopedTexture create_output_texture(WGPUDevice device, std::uint32_t width, std::uint32_t height) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    ScopedTexture texture{wgpuDeviceCreateTexture(device, &descriptor)};
    REQUIRE(texture.m_value != nullptr);
    return texture;
}

// Creates a copyable output texture for real-backend readback tests.
ScopedTexture create_readback_output_texture(WGPUDevice device, std::uint32_t width, std::uint32_t height) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    ScopedTexture texture{wgpuDeviceCreateTexture(device, &descriptor)};
    REQUIRE(texture.m_value != nullptr);
    return texture;
}

// Creates an RGBA16Float source texture with a bright white 2x2 center emitter.
ScopedTexture create_bright_hdr_source(ofg::GpuContext gpu, std::uint32_t width, std::uint32_t height) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = ofg::SceneColorTarget::format();
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    ScopedTexture texture{wgpuDeviceCreateTexture(gpu.m_device, &descriptor)};
    REQUIRE(texture.m_value != nullptr);

    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width) * height * 4U, 0);
    for (std::uint32_t y = height / 2U - 1U; y <= height / 2U; ++y) {
        for (std::uint32_t x = width / 2U - 1U; x <= width / 2U; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[index] = float_to_half_bits(8.0f);
            pixels[index + 1U] = float_to_half_bits(8.0f);
            pixels[index + 2U] = float_to_half_bits(8.0f);
            pixels[index + 3U] = float_to_half_bits(1.0f);
        }
    }

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture.m_value;
    destination.mipLevel = 0;
    destination.origin = WGPUOrigin3D{0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = width * 8U;
    layout.rowsPerImage = height;

    const WGPUExtent3D write_size{width, height, 1};
    wgpuQueueWriteTexture(
        gpu.m_queue, &destination, pixels.data(), pixels.size() * sizeof(std::uint16_t), &layout, &write_size);
    return texture;
}

// Creates a default view for a test texture.
ScopedTextureView create_output_view(WGPUTexture texture) {
    ScopedTextureView view{wgpuTextureCreateView(texture, nullptr)};
    REQUIRE(view.m_value != nullptr);
    return view;
}

// Creates a command encoder for direct render-pass tests.
ScopedCommandEncoder create_encoder(WGPUDevice device) {
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(device, nullptr)};
    REQUIRE(encoder.m_value != nullptr);
    return encoder;
}

// Reads an RGBA8 output texture after encoding a caller-supplied render path.
std::vector<std::uint8_t> render_and_readback(ofg::tests::TestGpuContext& gpu,
    WGPUTextureView source_view,
    ofg::BloomPass& bloom_pass,
    ofg::ToneMapPass& tone_map_pass,
    const std::optional<ofg::BloomSettings>& bloom_settings,
    std::uint32_t width,
    std::uint32_t height) {
    ScopedTexture output_texture = create_readback_output_texture(gpu.borrowed_context().m_device, width, height);
    ScopedTextureView output_view = create_output_view(output_texture.m_value);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    if (bloom_settings.has_value()) {
        ofg::TempBuffer::begin_frame();
        ofg::BloomResult result =
            bloom_pass.render(encoder.m_value, source_view, width, height, bloom_settings.value());
        tone_map_pass.render(encoder.m_value,
            source_view,
            result.tone_map_input(),
            ofg::RenderTarget{output_view.m_value, WGPUTextureFormat_RGBA8Unorm, width, height});
        ofg::TempBuffer::release(result.m_buffer);
        ofg::TempBuffer::end_frame();
    } else {
        tone_map_pass.render(encoder.m_value,
            source_view,
            ofg::RenderTarget{output_view.m_value, WGPUTextureFormat_RGBA8Unorm, width, height});
    }

    const std::uint32_t unpadded_bytes_per_row = width * 4U;
    const std::uint32_t padded_bytes_per_row = align_to(unpadded_bytes_per_row, 256U);
    const std::uint64_t readback_size = static_cast<std::uint64_t>(padded_bytes_per_row) * height;
    WGPUBufferDescriptor buffer_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    buffer_descriptor.size = readback_size;
    ScopedBuffer readback{wgpuDeviceCreateBuffer(gpu.borrowed_context().m_device, &buffer_descriptor)};
    REQUIRE(readback.m_value != nullptr);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = output_texture.m_value;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = readback.m_value;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = padded_bytes_per_row;
    destination.layout.rowsPerImage = height;

    const WGPUExtent3D copy_size{width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder.m_value, &source, &destination, &copy_size);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
    encoder.m_value = nullptr;
    REQUIRE(command != nullptr);
    ScopedCommandBuffer command_buffer{command};
    wgpuQueueSubmit(gpu.borrowed_context().m_queue, 1, &command_buffer.m_value);

    MapRequest map_request;
    WGPUBufferMapCallbackInfo map_callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    map_callback.callback = handle_map_request;
    map_callback.userdata1 = &map_request;
    std::string wait_error;
    REQUIRE(gpu.wait_for_future(
        wgpuBufferMapAsync(
            readback.m_value, WGPUMapMode_Read, 0, static_cast<std::size_t>(readback_size), map_callback),
        "mapAsync",
        wait_error));
    REQUIRE_MESSAGE(map_request.m_status == WGPUMapAsyncStatus_Success, map_request.m_message);

    const auto* mapped = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(readback.m_value, 0, static_cast<std::size_t>(readback_size)));
    REQUIRE(mapped != nullptr);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(unpadded_bytes_per_row) * height);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t src_start = static_cast<std::size_t>(row) * padded_bytes_per_row;
        const std::size_t dst_start = static_cast<std::size_t>(row) * unpadded_bytes_per_row;
        std::copy_n(mapped + src_start, unpadded_bytes_per_row, pixels.data() + dst_start);
    }
    wgpuBufferUnmap(readback.m_value);
    return pixels;
}

// Returns one RGBA8 pixel from a tightly packed readback image.
std::array<std::uint8_t, 4> pixel_at(
    const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
    return {pixels[index], pixels[index + 1U], pixels[index + 2U], pixels[index + 3U]};
}

} // namespace

// Verifies BloomPass durable WebGPU state and validation.
TEST_CASE("bloom pass creates durable GPU resources") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::BloomPass::create(ofg::GpuContext{}, ofg::SceneColorTarget::format()); }()),
        doctest::Contains("WebGPU device"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::BloomPass::create(gpu.borrowed_context(), WGPUTextureFormat_RGBA8Unorm); }()),
        doctest::Contains("RGBA16Float"),
        ofg::EngineError);

    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    REQUIRE(pass != nullptr);
    CHECK(pass->counters().m_shader_module_create_count == 2);
    CHECK(pass->counters().m_bind_group_layout_create_count == 2);
    CHECK(pass->counters().m_pipeline_create_count == 3);
    CHECK(pass->counters().m_buffer_create_count == 1);
    CHECK(pass->counters().m_bind_group_create_count == 0);
}

// Verifies disabled bloom skips TempBuffer and records a skipped diagnostic.
TEST_CASE("bloom pass skips disabled and zero intensity settings") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(8, 8);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    ofg::BloomSettings settings = ofg::default_bloom_settings();
    settings.m_enabled = false;
    ofg::BloomResult result = pass->render(encoder.m_value, scene_color.view(), 8, 8, settings);
    CHECK_FALSE(result.valid());
    CHECK(pass->diagnostics().m_skipped);
    CHECK(pass->counters().m_bind_group_create_count == 0);

    settings = ofg::default_bloom_settings();
    settings.m_intensity = 0.0f;
    result = pass->render(encoder.m_value, scene_color.view(), 8, 8, settings);
    CHECK_FALSE(result.valid());
    CHECK(pass->diagnostics().m_skipped);
    CHECK(pass->counters().m_bind_group_create_count == 0);
}

// Verifies BloomPass validates render inputs before acquiring temporary targets.
TEST_CASE("bloom pass rejects invalid render inputs") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(8, 8);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);
    const ofg::BloomSettings settings = ofg::default_bloom_settings();

    CHECK_THROWS_WITH_AS(([&]() { (void)pass->render(nullptr, scene_color.view(), 8, 8, settings); }()),
        doctest::Contains("encoder"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)pass->render(encoder.m_value, nullptr, 8, 8, settings); }()),
        doctest::Contains("scene color view"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)pass->render(encoder.m_value, scene_color.view(), 0, 8, settings); }()),
        doctest::Contains("nonzero"),
        ofg::EngineError);
}

// Verifies tiny viewports that cannot build the first level skip without touching TempBuffer.
TEST_CASE("bloom pass skips when pyramid plan is empty") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(2, 2);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    ofg::BloomSettings settings = ofg::default_bloom_settings();
    settings.m_min_level_extent = 2;
    ofg::BloomResult result = pass->render(encoder.m_value, scene_color.view(), 2, 2, settings);
    CHECK_FALSE(result.valid());
    CHECK(pass->diagnostics().m_skipped);
    CHECK(pass->counters().m_bind_group_create_count == 0);
}

// Verifies a one-level pyramid returns the prefilter target directly to tone mapping.
TEST_CASE("bloom pass returns a one level pyramid result") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(8, 8);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    ofg::BloomSettings settings = ofg::default_bloom_settings();
    settings.m_max_levels = 1;
    ofg::TempBuffer::begin_frame();
    ofg::BloomResult result = pass->render(encoder.m_value, scene_color.view(), 8, 8, settings);
    REQUIRE(result.valid());
    CHECK(result.m_width == 4U);
    CHECK(result.m_height == 4U);
    CHECK(pass->diagnostics().m_active_level_count == 1U);
    CHECK(pass->diagnostics().m_encoded_pass_count == 1U);
    CHECK(pass->diagnostics().m_draw_count == 1U);
    CHECK(ofg::TempBuffer::stats().m_active_count == 1U);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 0U);

    ofg::TempBuffer::release(result.m_buffer);
    ofg::TempBuffer::end_frame();
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 1U);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
    encoder.m_value = nullptr;
    REQUIRE(command != nullptr);
    ScopedCommandBuffer command_buffer{command};
}

// Verifies BloomPass encodes a small pyramid and ToneMapPass consumes the result.
TEST_CASE("bloom pass renders pyramid result for tone map composition") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    std::unique_ptr<ofg::BloomPass> bloom_pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    std::unique_ptr<ofg::ToneMapPass> tone_map_pass = ofg::ToneMapPass::create(
        gpu.borrowed_context(), WGPUTextureFormat_RGBA8Unorm, ofg::ToneMapOutputEncoding::ManualSrgb);
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(8, 8);
    ScopedTexture output_texture = create_output_texture(gpu.borrowed_context().m_device, 8, 8);
    ScopedTextureView output_view = create_output_view(output_texture.m_value);
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    ofg::TempBuffer::begin_frame();
    ofg::BloomSettings settings = ofg::default_bloom_settings();
    ofg::BloomResult result = bloom_pass->render(encoder.m_value, scene_color.view(), 8, 8, settings);
    REQUIRE(result.valid());
    CHECK(result.m_width == 4U);
    CHECK(result.m_height == 4U);
    CHECK(result.tone_map_input().m_view == result.view());
    CHECK(result.tone_map_input().m_intensity == doctest::Approx(settings.m_intensity));
    CHECK(bloom_pass->diagnostics().m_active_level_count == 2U);
    CHECK(bloom_pass->diagnostics().m_encoded_pass_count == 3U);
    CHECK(bloom_pass->diagnostics().m_draw_count == 3U);
    CHECK(bloom_pass->diagnostics().m_estimated_read_bytes > 0U);
    CHECK(bloom_pass->diagnostics().m_estimated_write_bytes > 0U);
    CHECK(bloom_pass->counters().m_bind_group_create_count == 3U);
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 3U);
    CHECK(ofg::TempBuffer::stats().m_active_count == 1U);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 2U);
    CHECK(ofg::TempBuffer::stats().m_early_release_count == 2U);

    tone_map_pass->render(encoder.m_value,
        scene_color.view(),
        result.tone_map_input(),
        ofg::RenderTarget{output_view.m_value, WGPUTextureFormat_RGBA8Unorm, 8, 8});
    CHECK(tone_map_pass->counters().m_bind_group_create_count == 1U);

    ofg::TempBuffer::release(result.m_buffer);
    CHECK_FALSE(result.valid());
    CHECK(result.tone_map_input().m_view == nullptr);
    ofg::TempBuffer::end_frame();
    CHECK(ofg::TempBuffer::stats().m_active_count == 0U);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 3U);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
    encoder.m_value = nullptr;
    REQUIRE(command != nullptr);
    ScopedCommandBuffer command_buffer{command};
}

// Verifies cached bind groups are replaced when same-scene pyramid source views rotate.
TEST_CASE("bloom pass replaces cached bind groups for changed pyramid sources") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    std::unique_ptr<ofg::BloomPass> pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(16, 16);

    ofg::BloomSettings settings = ofg::default_bloom_settings();
    settings.m_max_levels = 3;
    {
        ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);
        ofg::TempBuffer::begin_frame();
        ofg::BloomResult result = pass->render(encoder.m_value, scene_color.view(), 16, 16, settings);
        REQUIRE(result.valid());
        ofg::TempBuffer::release(result.m_buffer);
        ofg::TempBuffer::end_frame();
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
        encoder.m_value = nullptr;
        REQUIRE(command != nullptr);
        ScopedCommandBuffer command_buffer{command};
    }
    CHECK(pass->counters().m_bind_group_create_count == 5U);

    settings.m_initial_downscale = 4;
    {
        ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);
        ofg::TempBuffer::begin_frame();
        ofg::BloomResult result = pass->render(encoder.m_value, scene_color.view(), 16, 16, settings);
        REQUIRE(result.valid());
        ofg::TempBuffer::release(result.m_buffer);
        ofg::TempBuffer::end_frame();
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
        encoder.m_value = nullptr;
        REQUIRE(command != nullptr);
        ScopedCommandBuffer command_buffer{command};
    }
    CHECK(pass->counters().m_bind_group_create_count == 7U);
    CHECK(pass->diagnostics().m_active_level_count == 2U);
    CHECK(ofg::TempBuffer::stats().m_reused_count > 0U);
}

// Verifies bloom threshold, clamp, tint/intensity, and halo formation through real GPU pixels when available.
TEST_CASE("bloom pass produces tinted halo pixels on real backend when available") {
    TempBufferReset reset;
    std::string error;
    std::optional<ofg::tests::TestGpuContext> maybe_gpu =
        ofg::tests::TestGpuContext::create(error, WGPUBackendType_Vulkan);
    if (!maybe_gpu.has_value()) {
        MESSAGE("Skipping real-backend bloom readback: " << error);
        return;
    }
    ofg::tests::TestGpuContext gpu = std::move(*maybe_gpu);
    ofg::TempBuffer::create(gpu.borrowed_context());

    constexpr std::uint32_t width = 16;
    constexpr std::uint32_t height = 16;
    ScopedTexture source_texture = create_bright_hdr_source(gpu.borrowed_context(), width, height);
    ScopedTextureView source_view = create_output_view(source_texture.m_value);
    std::unique_ptr<ofg::BloomPass> bloom_pass =
        ofg::BloomPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format());
    std::unique_ptr<ofg::ToneMapPass> tone_map_pass = ofg::ToneMapPass::create(
        gpu.borrowed_context(), WGPUTextureFormat_RGBA8Unorm, ofg::ToneMapOutputEncoding::ManualSrgb);

    const std::vector<std::uint8_t> disabled =
        render_and_readback(gpu, source_view.m_value, *bloom_pass, *tone_map_pass, std::nullopt, width, height);

    ofg::BloomSettings enabled = ofg::default_bloom_settings();
    enabled.m_intensity = 0.45f;
    enabled.m_tint = ofg::math::vec3(0.2f, 0.4f, 1.0f);
    const std::vector<std::uint8_t> tinted =
        render_and_readback(gpu, source_view.m_value, *bloom_pass, *tone_map_pass, enabled, width, height);

    ofg::BloomSettings thresholded = enabled;
    thresholded.m_threshold = 16.0f;
    thresholded.m_soft_knee = 0.0f;
    const std::vector<std::uint8_t> high_threshold =
        render_and_readback(gpu, source_view.m_value, *bloom_pass, *tone_map_pass, thresholded, width, height);

    ofg::BloomSettings clamped = enabled;
    clamped.m_clamp = 1.0f;
    const std::vector<std::uint8_t> low_clamp =
        render_and_readback(gpu, source_view.m_value, *bloom_pass, *tone_map_pass, clamped, width, height);

    const std::array<std::uint8_t, 4> disabled_halo = pixel_at(disabled, width, 5, 5);
    const std::array<std::uint8_t, 4> tinted_halo = pixel_at(tinted, width, 5, 5);
    const std::array<std::uint8_t, 4> threshold_halo = pixel_at(high_threshold, width, 5, 5);
    const std::array<std::uint8_t, 4> clamped_halo = pixel_at(low_clamp, width, 5, 5);

    CHECK(tinted_halo[2] > disabled_halo[2] + 5U);
    CHECK(tinted_halo[2] > tinted_halo[0] + 5U);
    CHECK(tinted_halo[2] > tinted_halo[1] + 3U);
    CHECK(threshold_halo[2] <= disabled_halo[2] + 3U);
    CHECK(tinted_halo[2] > clamped_halo[2] + 5U);
}
