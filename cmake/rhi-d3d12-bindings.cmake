# Cache leaf parameter-block snapshots in the pinned RHI's otherwise empty per-command binding cache.
# Generate a complete D3D12 directory so every translation unit sees the same adapted header layout.
# Upstream files and license notices are preserved; only the two verified anchors below change.
set(ofg_d3d12_source "${CMAKE_CURRENT_SOURCE_DIR}/slang-rhi/src/d3d12")
set(ofg_d3d12_output "${CMAKE_CURRENT_BINARY_DIR}/ofg-d3d12")
file(GLOB ofg_d3d12_files CONFIGURE_DEPENDS "${ofg_d3d12_source}/*.h" "${ofg_d3d12_source}/*.cpp")
get_target_property(ofg_rhi_sources slang-rhi SOURCES)
foreach(ofg_file IN LISTS ofg_d3d12_files)
    get_filename_component(ofg_name "${ofg_file}" NAME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ofg_file}")
    file(READ "${ofg_file}" ofg_text)
    if(ofg_name STREQUAL "d3d12-shader-object.h")
        set(ofg_anchor "struct BindingCache\n{\n    void reset() {}\n};")
        set(ofg_replacement [=[
// OFG: snapshots live exactly as long as their command buffer's descriptor/constant arenas.
struct BindingCache
{
    struct Entry
    {
        RefPtr<ShaderObject> object;
        RefPtr<ShaderObjectLayoutImpl> layout;
        DescriptorSet descriptors;
        std::vector<BindingDataImpl::BufferState> buffers;
        std::vector<BindingDataImpl::TextureState> textures;
    };
    std::map<std::tuple<ShaderObject*, uint32_t, ShaderObjectLayoutImpl*>, Entry> leaves;
    // Drop snapshot identity/ownership when the GPU-completed command buffer is reset.
    void reset() { leaves.clear(); }
};]=])
        string(REPLACE "#include <vector>" "#include <vector>\n#include <map>\n#include <tuple>" ofg_text "${ofg_text}")
    elseif(ofg_name STREQUAL "d3d12-shader-object.cpp")
        set(ofg_anchor [=[    // The first step to binding an object as a parameter block is to allocate a descriptor
    // set (consisting of zero or one resource descriptor table and zero or one sampler
    // descriptor table) to represent its values.
    //
    BindingOffset subOffset = offset;

    DescriptorSet descriptorSet;

    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, /* inout */ subOffset, specializedLayout, descriptorSet));

    // Next we bind the object into that descriptor set as if it were being used
    // as a `ConstantBuffer<X>`.
    //
    SLANG_RETURN_ON_FAIL(
        bindAsConstantBuffer(shaderObject, descriptorSet, subOffset, rootParamIndex, specializedLayout)
    );

    return SLANG_OK;]=])
        set(ofg_replacement [=[    // OFG: leaf blocks have no independently mutable children or user root descriptors.
    // Versioned snapshots preserve interleaved edits without trusting finalize(), which does not
    // set m_finalized in this upstream revision. Nested blocks retain the upstream traversal.
    const bool cacheable = specializedLayout->m_subObjectRanges.empty() &&
                           specializedLayout->getOwnUserRootParameterCount() == 0;
    // Retaining the object prevents pointer reuse; upstream m_uid is not initialized in this revision.
    const auto key = std::make_tuple(shaderObject, shaderObject->m_version, specializedLayout);
    if (cacheable)
    {
        auto found = m_bindingCache->leaves.find(key);
        if (found != m_bindingCache->leaves.end())
        {
            const auto& entry = found->second;
            uint32_t index = offset.rootParam;
            if (entry.descriptors.resources.count)
            {
                m_bindingData->rootParameters[index] =
                    createRootDescriptorTable(index, entry.descriptors.resources.firstGpuHandle);
                ++index;
            }
            if (entry.descriptors.samplers.count)
                m_bindingData->rootParameters[index] =
                    createRootDescriptorTable(index, entry.descriptors.samplers.firstGpuHandle);
            for (const auto& state : entry.buffers)
                writeBufferState(this, state.buffer, state.state);
            for (const auto& state : entry.textures)
                writeTextureState(this, state.textureView, state.state);
            return SLANG_OK;
        }
    }
    const uint32_t firstBuffer = m_bindingData->bufferStateCount;
    const uint32_t firstTexture = m_bindingData->textureStateCount;
    BindingOffset subOffset = offset;
    DescriptorSet descriptorSet{};
    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, subOffset, specializedLayout, descriptorSet));
    SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(shaderObject, descriptorSet, subOffset, rootParamIndex, specializedLayout));
    if (cacheable)
    {
        BindingCache::Entry entry;
        entry.object = shaderObject;
        entry.layout = specializedLayout;
        entry.descriptors = descriptorSet;
        entry.buffers.assign(m_bindingData->bufferStates + firstBuffer,
                             m_bindingData->bufferStates + m_bindingData->bufferStateCount);
        entry.textures.assign(m_bindingData->textureStates + firstTexture,
                              m_bindingData->textureStates + m_bindingData->textureStateCount);
        m_bindingCache->leaves.emplace(key, std::move(entry));
    }
    return SLANG_OK;]=])
    else()
        set(ofg_anchor "")
    endif()
    if(NOT ofg_anchor STREQUAL "")
        string(FIND "${ofg_text}" "${ofg_anchor}" ofg_index)
        if(ofg_index EQUAL -1)
            message(FATAL_ERROR "Pinned RHI D3D12 binding-cache anchor changed: ${ofg_name}")
        endif()
        string(REPLACE "${ofg_anchor}" "${ofg_replacement}" ofg_text "${ofg_text}")
    endif()
    file(CONFIGURE OUTPUT "${ofg_d3d12_output}/${ofg_name}" CONTENT "${ofg_text}" @ONLY)
    if("src/d3d12/${ofg_name}" IN_LIST ofg_rhi_sources)
        list(REMOVE_ITEM ofg_rhi_sources "src/d3d12/${ofg_name}")
        list(APPEND ofg_rhi_sources "${ofg_d3d12_output}/${ofg_name}")
    endif()
endforeach()
set_property(TARGET slang-rhi PROPERTY SOURCES "${ofg_rhi_sources}")
# Relative parent includes in the generated files still resolve to the pinned common RHI headers.
target_include_directories(slang-rhi PRIVATE "${ofg_d3d12_source}")
