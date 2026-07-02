// Mutable WGSL shader resource for the OFG renderer.
#include "ofg/resources/shader.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Validates that a shader layout has named, unique parameters.
void validate_parameter_layout(const ShaderParameterLayout& layout) {
    std::unordered_set<std::string> names;
    for (const ShaderParameter& parameter : layout.m_parameters) {
        if (parameter.m_name.empty()) {
            throw EngineError("Shader parameter names must not be empty.");
        }
        if (!names.insert(parameter.m_name).second) {
            throw EngineError("Shader parameter '" + parameter.m_name + "' is declared more than once.");
        }
    }
}

// Creates a WebGPU shader module from WGSL source for a ready GPU context.
WGPUShaderModule create_shader_module(const GpuContext& context, const std::string& label, const std::string& source) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::string_view(source);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::string_view(label);

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(context.m_device, &descriptor);
    if (module == nullptr) {
        throw EngineError("wgpuDeviceCreateShaderModule returned null for shader '" + label + "'.");
    }
    return module;
}

} // namespace

// Allocates a labeled shader resource using the creating Resources context.
Shader::Shader(GpuContext gpu, std::string label) : m_gpu(std::move(gpu)), m_label(std::move(label)) {
    if (m_label.empty()) {
        throw EngineError("Shader label must not be empty.");
    }
}

// Releases the owned WebGPU shader module.
Shader::~Shader() {
    release_gpu_state();
}

// Initializes this shader from WGSL source and explicit parameter layout.
void Shader::init_from_wgsl(
    std::string wgsl_source, ShaderParameterLayout parameter_layout, std::vector<PipelineDefinition> pipelines) {
    if (wgsl_source.empty()) {
        throw EngineError("Shader WGSL source must not be empty.");
    }
    validate_parameter_layout(parameter_layout);
    release_gpu_state();
    m_wgsl_source = std::move(wgsl_source);
    m_parameter_layout = std::move(parameter_layout);
    m_pipelines = std::move(pipelines);
    prepare_gpu_state();
    m_revision += 1;
}

// Replaces WGSL source and increments the shader revision.
void Shader::replace_source(std::string wgsl_source) {
    if (wgsl_source.empty()) {
        throw EngineError("Shader WGSL source must not be empty.");
    }
    WGPUShaderModule next_module = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            throw EngineError("Shader GPU preparation requires a WebGPU device and queue.");
        }
        next_module = create_shader_module(m_gpu, m_label, wgsl_source);
    }
    release_gpu_state();
    m_module = next_module;
    m_wgsl_source = std::move(wgsl_source);
    m_revision += 1;
}

// Finds a declared shader parameter by name.
const ShaderParameter* Shader::parameter(std::string_view name) const noexcept {
    for (const ShaderParameter& parameter : m_parameter_layout.m_parameters) {
        if (parameter.m_name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

// Returns all declared parameters.
std::span<const ShaderParameter> Shader::parameters() const noexcept {
    return m_parameter_layout.m_parameters;
}

// Returns parameters for one binding scope.
std::vector<ShaderParameter> Shader::parameters_for_scope(ShaderParameterScope scope) const {
    std::vector<ShaderParameter> scoped;
    for (const ShaderParameter& parameter : m_parameter_layout.m_parameters) {
        if (parameter.m_scope == scope) {
            scoped.push_back(parameter);
        }
    }
    return scoped;
}

// Returns the shader module, null for CPU-only resources.
WGPUShaderModule Shader::module() const noexcept {
    return m_module;
}

// Returns the current source revision.
std::uint64_t Shader::revision() const noexcept {
    return m_revision;
}

// Returns the shader label.
const std::string& Shader::label() const noexcept {
    return m_label;
}

// Returns the WGSL source.
const std::string& Shader::source() const noexcept {
    return m_wgsl_source;
}

// Creates or recreates the WebGPU shader module from WGSL source.
void Shader::prepare_gpu_state() {
    if (gpu_context_is_empty(m_gpu)) {
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Shader GPU preparation requires a WebGPU device and queue.");
    }

    WGPUShaderModule next_module = create_shader_module(m_gpu, m_label, m_wgsl_source);
    release_gpu_state();
    m_module = next_module;
}

// Releases the owned WebGPU shader module.
void Shader::release_gpu_state() noexcept {
    if (m_module != nullptr) {
        wgpuShaderModuleRelease(m_module);
        m_module = nullptr;
    }
}

// Converts a shader parameter type into a readable diagnostic label.
const char* shader_parameter_type_name(ShaderParameterType type) noexcept {
    switch (type) {
    case ShaderParameterType::Float:
        return "float";
    case ShaderParameterType::Vec2:
        return "vec2";
    case ShaderParameterType::Vec3:
        return "vec3";
    case ShaderParameterType::Vec4:
        return "vec4";
    case ShaderParameterType::Mat4:
        return "mat4";
    case ShaderParameterType::Texture:
        return "texture";
    }
    return "unknown";
}

// Converts a shader parameter scope into a readable diagnostic label.
const char* shader_parameter_scope_name(ShaderParameterScope scope) noexcept {
    switch (scope) {
    case ShaderParameterScope::Frame:
        return "frame";
    case ShaderParameterScope::Draw:
        return "draw";
    case ShaderParameterScope::Material:
        return "material";
    }
    return "unknown";
}

} // namespace ofg
