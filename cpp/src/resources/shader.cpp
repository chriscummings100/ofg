// Mutable WGSL shader resource for the OFG renderer.
#include "ofg/resources/shader.hpp"

#include "ofg/render/webgpu_common.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Validates that a shader layout has named, unique parameters.
bool validate_parameter_layout(const ShaderParameterLayout& layout, std::string& error) {
    std::unordered_set<std::string> names;
    for (const ShaderParameter& parameter : layout.m_parameters) {
        if (parameter.m_name.empty()) {
            error = "Shader parameter names must not be empty.";
            return false;
        }
        if (!names.insert(parameter.m_name).second) {
            error = "Shader parameter '" + parameter.m_name + "' is declared more than once.";
            return false;
        }
    }
    return true;
}

// Creates a WebGPU shader module from WGSL source for a ready GPU context.
WGPUShaderModule create_shader_module(
    const GpuContext& context, const std::string& label, const std::string& source, std::string& error) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::string_view(source);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::string_view(label);

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(context.m_device, &descriptor);
    if (module == nullptr) {
        error = "wgpuDeviceCreateShaderModule returned null for shader '" + label + "'.";
    }
    return module;
}

} // namespace

// Stores validated shader CPU data; use create() for validation.
Shader::Shader(GpuContext gpu,
    std::string label,
    std::string wgsl_source,
    ShaderParameterLayout parameter_layout,
    std::vector<PipelineDefinition> pipelines)
    : m_gpu(std::move(gpu)), m_label(std::move(label)), m_wgsl_source(std::move(wgsl_source)),
      m_parameter_layout(std::move(parameter_layout)), m_pipelines(std::move(pipelines)) {}

// Moves shader CPU and GPU handles without duplicating ownership.
Shader::Shader(Shader&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_label(std::move(other.m_label)), m_wgsl_source(std::move(other.m_wgsl_source)),
      m_parameter_layout(std::move(other.m_parameter_layout)), m_pipelines(std::move(other.m_pipelines)),
      m_module(other.m_module), m_revision(other.m_revision) {
    other.m_module = nullptr;
}

// Moves shader CPU and GPU handles without duplicating ownership.
Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        release_gpu_state();
        m_gpu = std::move(other.m_gpu);
        m_label = std::move(other.m_label);
        m_wgsl_source = std::move(other.m_wgsl_source);
        m_parameter_layout = std::move(other.m_parameter_layout);
        m_pipelines = std::move(other.m_pipelines);
        m_module = other.m_module;
        m_revision = other.m_revision;
        other.m_module = nullptr;
    }
    return *this;
}

// Releases the owned WebGPU shader module.
Shader::~Shader() {
    release_gpu_state();
}

// Creates a shader resource and validates its declared layout.
std::optional<Shader> Shader::create(GpuContext gpu,
    std::string label,
    std::string wgsl_source,
    ShaderParameterLayout parameter_layout,
    std::vector<PipelineDefinition> pipelines,
    std::string& error) {
    if (label.empty()) {
        error = "Shader label must not be empty.";
        return std::nullopt;
    }
    if (wgsl_source.empty()) {
        error = "Shader WGSL source must not be empty.";
        return std::nullopt;
    }
    if (!validate_parameter_layout(parameter_layout, error)) {
        return std::nullopt;
    }
    Shader shader(
        std::move(gpu), std::move(label), std::move(wgsl_source), std::move(parameter_layout), std::move(pipelines));
    if (!shader.prepare_gpu_state(error)) {
        return std::nullopt;
    }
    error.clear();
    return shader;
}

// Replaces WGSL source and increments the shader revision.
bool Shader::replace_source(std::string wgsl_source, std::string& error) {
    if (wgsl_source.empty()) {
        error = "Shader WGSL source must not be empty.";
        return false;
    }
    WGPUShaderModule next_module = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            error = "Shader GPU preparation requires a WebGPU device and queue.";
            return false;
        }
        next_module = create_shader_module(m_gpu, m_label, wgsl_source, error);
        if (next_module == nullptr) {
            return false;
        }
    }
    release_gpu_state();
    m_module = next_module;
    m_wgsl_source = std::move(wgsl_source);
    m_revision += 1;
    error.clear();
    return true;
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
bool Shader::prepare_gpu_state(std::string& error) {
    if (gpu_context_is_empty(m_gpu)) {
        error.clear();
        return true;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        error = "Shader GPU preparation requires a WebGPU device and queue.";
        return false;
    }

    WGPUShaderModule next_module = create_shader_module(m_gpu, m_label, m_wgsl_source, error);
    if (next_module == nullptr) {
        return false;
    }
    release_gpu_state();
    m_module = next_module;
    error.clear();
    return true;
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
