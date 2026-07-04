// Procedural sky render pass implementation.
#include "ofg/render/sky_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/environment.hpp"

#include "shaders/procedural_sky.wgsl.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ofg {
namespace {

constexpr std::uint64_t _sky_uniform_bytes = sizeof(float) * 48U;
constexpr float _default_sun_intensity = 3.2f;

// Creates a shader module from the built-in procedural-sky WGSL source.
WGPUShaderModule create_sky_shader_module(WGPUDevice device) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::cstring_view(render::shaders::procedural_sky_wgsl);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::cstring_view("OFG procedural sky shader");

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
    if (module == nullptr) {
        throw EngineError("wgpuDeviceCreateShaderModule returned null for sky pass.");
    }
    return module;
}

// Creates the uniform bind group layout consumed by the sky shader.
WGPUBindGroupLayout create_sky_bind_group_layout(WGPUDevice device) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = WGPU_FALSE;
    entry.buffer.minBindingSize = _sky_uniform_bytes;

    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG sky bind group layout");
    descriptor.entryCount = 1;
    descriptor.entries = &entry;

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroupLayout returned null for sky pass.");
    }
    return layout;
}

// Creates the pipeline layout for the sky pass.
WGPUPipelineLayout create_sky_pipeline_layout(WGPUDevice device, WGPUBindGroupLayout bind_group_layout) {
    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG sky pipeline layout");
    descriptor.bindGroupLayoutCount = 1;
    descriptor.bindGroupLayouts = &bind_group_layout;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreatePipelineLayout returned null for sky pass.");
    }
    return layout;
}

// Creates the fullscreen-triangle sky pipeline.
WGPURenderPipeline create_sky_pipeline(WGPUDevice device,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    WGPUTextureFormat color_format,
    WGPUTextureFormat depth_format) {
    WGPUVertexState vertex_state = WGPU_VERTEX_STATE_INIT;
    vertex_state.module = module;
    vertex_state.entryPoint = gpu::cstring_view("vs_main");

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = color_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
    fragment_state.module = module;
    fragment_state.entryPoint = gpu::cstring_view("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth_stencil.format = depth_format;
    depth_stencil.depthWriteEnabled = WGPUOptionalBool_False;
    depth_stencil.depthCompare = WGPUCompareFunction_Equal;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG sky pipeline");
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.depthStencil = &depth_stencil;
    descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    if (pipeline == nullptr) {
        throw EngineError("wgpuDeviceCreateRenderPipeline returned null for sky pass.");
    }
    return pipeline;
}

// Creates the persistent sky uniform buffer.
WGPUBuffer create_sky_uniform_buffer(WGPUDevice device) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG sky uniforms");
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = _sky_uniform_bytes;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError("wgpuDeviceCreateBuffer returned null for sky pass.");
    }
    return buffer;
}

// Creates the persistent sky bind group.
WGPUBindGroup create_sky_bind_group(WGPUDevice device, WGPUBindGroupLayout layout, WGPUBuffer uniform_buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = uniform_buffer;
    entry.offset = 0;
    entry.size = _sky_uniform_bytes;

    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG sky bind group");
    descriptor.layout = layout;
    descriptor.entryCount = 1;
    descriptor.entries = &entry;

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &descriptor);
    if (bind_group == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroup returned null for sky pass.");
    }
    return bind_group;
}

// Returns one world-space basis vector from a matrix column.
math::Vec3 matrix_column_xyz(const math::Mat4& matrix, std::size_t column) noexcept {
    return math::vec3(matrix[column].x, matrix[column].y, matrix[column].z);
}

// Returns a normalized vector or throws a clear packing error.
math::Vec3 normalize_required(math::Vec3 value, const char* label) {
    std::string error;
    const std::optional<math::Vec3> normalized = math::normalize(value, error);
    if (!normalized.has_value()) {
        throw EngineError(error.empty() ? std::string(label) + " must be nonzero." : error);
    }
    return *normalized;
}

// Returns the first directional light item, if present.
const LightProperties* first_directional_light(std::span<const LightProperties> lights) noexcept {
    for (const LightProperties& light : lights) {
        if (light.m_type == LightPropertiesType::Directional) {
            return &light;
        }
    }
    return nullptr;
}

} // namespace

// Packs camera, sun, and environment state into the WGSL sky uniform layout.
SkyPassUniforms build_sky_pass_uniforms(
    const CameraProperties& camera, const Environment& environment, std::span<const LightProperties> lights) {
    if (!std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect) ||
        camera.vertical_fov_radians <= 0.0f || camera.aspect <= 0.0f) {
        throw EngineError("SkyPass requires finite positive camera field of view and aspect.");
    }

    const float tan_half_fov = std::tan(camera.vertical_fov_radians * 0.5f);
    if (!std::isfinite(tan_half_fov) || tan_half_fov <= 0.0f) {
        throw EngineError("SkyPass camera field of view produced an invalid tangent.");
    }

    const math::Vec3 camera_right = normalize_required(matrix_column_xyz(camera.world_from_camera, 0), "Camera right");
    const math::Vec3 camera_up = normalize_required(matrix_column_xyz(camera.world_from_camera, 1), "Camera up");
    const math::Vec3 camera_forward =
        normalize_required(matrix_column_xyz(camera.world_from_camera, 2), "Camera forward");
    const math::Vec3 sun_direction = normalize_required(environment.sun_direction(), "Environment sun direction");
    const math::Vec3 moon_direction = normalize_required(environment.moon_direction(), "Environment moon direction");

    math::Vec3 sun_color{1.0f, 0.90f, 0.72f};
    float sun_intensity = _default_sun_intensity * environment.day_factor();
    if (const LightProperties* light = first_directional_light(lights); light != nullptr) {
        sun_color = light->m_color;
        sun_intensity = light->m_intensity;
    }

    const SkyWeather& weather = environment.weather();
    SkyPassUniforms uniforms;
    uniforms.m_values[0] = camera_right.x;
    uniforms.m_values[1] = camera_right.y;
    uniforms.m_values[2] = camera_right.z;
    uniforms.m_values[3] = tan_half_fov * camera.aspect;
    uniforms.m_values[4] = camera_up.x;
    uniforms.m_values[5] = camera_up.y;
    uniforms.m_values[6] = camera_up.z;
    uniforms.m_values[7] = tan_half_fov;
    uniforms.m_values[8] = camera_forward.x;
    uniforms.m_values[9] = camera_forward.y;
    uniforms.m_values[10] = camera_forward.z;
    uniforms.m_values[11] = 0.0f;
    uniforms.m_values[12] = sun_direction.x;
    uniforms.m_values[13] = sun_direction.y;
    uniforms.m_values[14] = sun_direction.z;
    uniforms.m_values[15] = 0.0f;
    uniforms.m_values[16] = sun_color.x;
    uniforms.m_values[17] = sun_color.y;
    uniforms.m_values[18] = sun_color.z;
    uniforms.m_values[19] = sun_intensity;
    uniforms.m_values[20] = moon_direction.x;
    uniforms.m_values[21] = moon_direction.y;
    uniforms.m_values[22] = moon_direction.z;
    uniforms.m_values[23] = environment.moon_phase();
    uniforms.m_values[24] = environment.day_factor();
    uniforms.m_values[25] = environment.twilight_factor();
    uniforms.m_values[26] = weather.m_haze;
    uniforms.m_values[27] = environment.time_seconds();
    uniforms.m_values[28] = weather.m_cloud_coverage;
    uniforms.m_values[29] = weather.m_storm_intensity;
    uniforms.m_values[30] = weather.m_cloud_opacity;
    uniforms.m_values[31] = weather.m_precipitation_hint;
    uniforms.m_values[32] = weather.m_wind_direction.x;
    uniforms.m_values[33] = weather.m_wind_direction.z;
    uniforms.m_values[34] = weather.m_wind_speed;
    uniforms.m_values[35] = weather.m_cloud_scale;
    uniforms.m_values[36] = weather.m_cloud_height;
    uniforms.m_values[37] = weather.m_cloud_sharpness;
    uniforms.m_values[38] = static_cast<float>(environment.star_seed());
    uniforms.m_values[39] = 0.0f;
    return uniforms;
}

// Stores already-created pass GPU state.
SkyPass::SkyPass(GpuContext gpu,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_group_layout,
    WGPUPipelineLayout pipeline_layout,
    WGPURenderPipeline pipeline,
    WGPUBuffer uniform_buffer,
    WGPUBindGroup bind_group)
    : m_gpu(std::move(gpu)), m_shader_module(shader_module), m_bind_group_layout(bind_group_layout),
      m_pipeline_layout(pipeline_layout), m_pipeline(pipeline), m_uniform_buffer(uniform_buffer),
      m_bind_group(bind_group) {}

// Releases owned WebGPU resources.
SkyPass::~SkyPass() {
    release_gpu_state();
}

// Creates shader, layout, pipeline, and persistent uniforms for sky rendering.
std::unique_ptr<SkyPass> SkyPass::create(
    GpuContext gpu, WGPUTextureFormat color_format, WGPUTextureFormat depth_format) {
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("SkyPass requires a WebGPU device and queue.");
    }
    if (color_format == WGPUTextureFormat_Undefined || depth_format == WGPUTextureFormat_Undefined) {
        throw EngineError("SkyPass requires defined color and depth formats.");
    }

    WGPUShaderModule shader_module = nullptr;
    WGPUBindGroupLayout bind_group_layout = nullptr;
    WGPUPipelineLayout pipeline_layout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer uniform_buffer = nullptr;
    WGPUBindGroup bind_group = nullptr;

    try {
        shader_module = create_sky_shader_module(gpu.m_device);
        bind_group_layout = create_sky_bind_group_layout(gpu.m_device);
        pipeline_layout = create_sky_pipeline_layout(gpu.m_device, bind_group_layout);
        pipeline = create_sky_pipeline(gpu.m_device, pipeline_layout, shader_module, color_format, depth_format);
        uniform_buffer = create_sky_uniform_buffer(gpu.m_device);
        bind_group = create_sky_bind_group(gpu.m_device, bind_group_layout, uniform_buffer);
    } catch (...) {
        if (bind_group != nullptr) {
            wgpuBindGroupRelease(bind_group);
        }
        if (uniform_buffer != nullptr) {
            wgpuBufferRelease(uniform_buffer);
        }
        if (pipeline != nullptr) {
            wgpuRenderPipelineRelease(pipeline);
        }
        if (pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(pipeline_layout);
        }
        if (bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(bind_group_layout);
        }
        if (shader_module != nullptr) {
            wgpuShaderModuleRelease(shader_module);
        }
        throw;
    }

    std::unique_ptr<SkyPass> pass(new SkyPass(
        std::move(gpu), shader_module, bind_group_layout, pipeline_layout, pipeline, uniform_buffer, bind_group));
    pass->m_counters.m_shader_module_create_count = 1;
    pass->m_counters.m_bind_group_layout_create_count = 1;
    pass->m_counters.m_pipeline_create_count = 1;
    pass->m_counters.m_buffer_create_count = 1;
    pass->m_counters.m_bind_group_create_count = 1;
    return pass;
}

// Encodes the sky fullscreen draw into the caller-owned scene render pass.
void SkyPass::draw(WGPURenderPassEncoder pass,
    const CameraProperties& camera,
    const Environment& environment,
    std::span<const LightProperties> lights) {
    if (pass == nullptr) {
        throw EngineError("SkyPass draw requires an open render pass.");
    }

    const SkyPassUniforms uniforms = build_sky_pass_uniforms(camera, environment, lights);
    wgpuQueueWriteBuffer(
        m_gpu.m_queue, m_uniform_buffer, 0, uniforms.m_values.data(), sizeof(float) * uniforms.m_values.size());

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

// Reports durable resource creation counters.
RendererCounters SkyPass::counters() const noexcept {
    return m_counters;
}

// Releases all WebGPU handles owned by this pass.
void SkyPass::release_gpu_state() noexcept {
    if (m_bind_group != nullptr) {
        wgpuBindGroupRelease(m_bind_group);
        m_bind_group = nullptr;
    }
    if (m_uniform_buffer != nullptr) {
        wgpuBufferRelease(m_uniform_buffer);
        m_uniform_buffer = nullptr;
    }
    if (m_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_pipeline_layout);
        m_pipeline_layout = nullptr;
    }
    if (m_bind_group_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_bind_group_layout);
        m_bind_group_layout = nullptr;
    }
    if (m_shader_module != nullptr) {
        wgpuShaderModuleRelease(m_shader_module);
        m_shader_module = nullptr;
    }
}

} // namespace ofg
