# OFG terrain laboratory: project setup and external dependencies.
cmake_minimum_required(VERSION 3.24)
project(ofg LANGUAGES C CXX)

option(OFG_TERRAIN_FUNCTION_PROFILE "Compile opt-in terrain function timing diagnostics" OFF)
if(OFG_TERRAIN_FUNCTION_PROFILE)
    add_compile_definitions(OFG_TERRAIN_FUNCTION_PROFILE=1)
endif()

option(OFG_BUILD_APP "Build the graphics application and GPU integration tests" ON)

if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions -pthread)
    add_link_options(-pthread)
elseif(OFG_BUILD_APP AND NOT WIN32)
    message(FATAL_ERROR "OFG supports native Windows and Emscripten browser builds.")
endif()

if(NOT EMSCRIPTEN)
    include(CTest)
endif()
add_subdirectory(external)

# PBR source modules have a fixed dependency/order list and are embedded on every target.
set(PBR_SHADER "")
foreach(module scene common brdf iridescence material ../sky/clouds ../shadows/sampling lighting mesh)
    get_filename_component(module_path shaders/pbr/${module}.slang ABSOLUTE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${module_path}")
    file(READ shaders/pbr/${module}.slang source)
    string(APPEND PBR_SHADER "\n#line 1 \"pbr/${module}.slang\"\n${source}\n")
endforeach()
configure_file(src/resources/pbr-shader.h.in generated/pbr-shader.h @ONLY)
file(READ shaders/pbr/ofg_scene_globals.slang SCENE_GLOBALS_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/pbr/ofg_scene_globals.slang)
configure_file(src/render/scene-globals-shader.h.in generated/scene-globals-shader.h @ONLY)
file(READ assets/pbr-studio.bin IBL_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," IBL_BYTES "${IBL_HEX}")
configure_file(src/lab/pbr-environment.h.in generated/pbr-environment.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/pbr-studio.bin)

add_library(ofg-core STATIC
    src/terrain/terrain-address.cpp
    src/terrain/terrain-content.cpp
    src/terrain/terrain-shading.cpp
    src/terrain/terrain-generation-client.cpp
    src/terrain/terrain-data-cache.cpp
    src/platform/http-client.cpp
    src/terrain/terrain-generator.cpp
    src/terrain/terrain-stream.cpp
    src/scene/outdoor-lighting.cpp
    src/resources/model.cpp
    src/resources/gltf-accessor.cpp
    src/resources/gltf-transform.cpp
    src/resources/gltf-material.cpp
    src/resources/gltf-mesh.cpp
    src/resources/gltf-import.cpp
    src/scene/model-instance.cpp
    src/resources/skin.cpp
    src/resources/animation.cpp
    src/scene/animator.cpp
    src/scene/animation-pose.cpp
    src/scene/animation-binding.cpp
    src/resources/pbr-material.cpp
    src/resources/environment.cpp
    src/lab/fly-camera.cpp
    src/lab/render-settings.cpp
    src/lab/sphere.cpp
    src/core/object.cpp
    src/math/mat.cpp
    src/math/quat.cpp
    src/math/transform.cpp
    src/resources/texture.cpp
    src/resources/sampler.cpp
    src/resources/image-decoder.cpp
    src/resources/mesh.cpp
    src/resources/material.cpp
    src/resources/shader.cpp
    src/render/draw-list.cpp
    src/render/shadow-cascades.cpp
    src/scene/camera.cpp
    src/scene/mesh-renderer.cpp
    src/resources/resource.cpp
    src/resources/resources.cpp
    src/scene/component.cpp
    src/scene/entity.cpp
    src/scene/scene.cpp
    src/state/state.cpp
)
target_compile_features(ofg-core PUBLIC cxx_std_20)
target_include_directories(ofg-core PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(ofg-core SYSTEM PRIVATE external/stb external/tinygltf)
if(WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(ofg-core PUBLIC winhttp)
endif()
target_sources(ofg-core PRIVATE src/terrain/terrain-workers.cpp src/terrain/terrain-streaming-service.cpp)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/checker.png assets/checker.jpg assets/README.md)
file(COPY assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_executable(ofg-terrain-test tests/core-test-main.cpp tests/terrain-content-test.cpp tests/terrain-http-test.cpp tests/terrain-stream-test.cpp tests/terrain-band-test.cpp tests/terrain-generator-test.cpp tests/terrain-workers-test.cpp tests/terrain-streaming-service-test.cpp)
    target_compile_definitions(ofg-terrain-test PRIVATE OFG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(ofg-terrain-allocation-test tests/core-test-main.cpp tests/terrain-allocation-test.cpp
        src/terrain/terrain-stream.cpp src/terrain/terrain-address.cpp)
    target_link_libraries(ofg-terrain-allocation-test PRIVATE doctest)
    target_compile_features(ofg-terrain-allocation-test PRIVATE cxx_std_20)
    target_include_directories(ofg-terrain-allocation-test PRIVATE src)
    # MSVC debug iterator proxies allocate in noexcept container constructors, so fault those containers
    # in the standard non-debug-iterator ABI. All translation units here use the same setting.
    if(MSVC)
        target_compile_definitions(ofg-terrain-allocation-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    endif()
    add_test(NAME ofg-terrain-allocation COMMAND ofg-terrain-allocation-test)
    set_tests_properties(ofg-terrain-allocation PROPERTIES LABELS "cpu;terrain" TIMEOUT 60)
    target_link_libraries(ofg-terrain-test PRIVATE ofg-core doctest)
    add_test(NAME ofg-terrain-fast COMMAND ofg-terrain-test --test-suite=terrain-fast)
    add_test(NAME ofg-terrain-bands COMMAND ofg-terrain-test --test-suite=terrain-bands)
    set_tests_properties(ofg-terrain-bands PROPERTIES LABELS "cpu;terrain" TIMEOUT 180)
    add_test(NAME ofg-terrain-exhaustive COMMAND ofg-terrain-test --test-suite=terrain-exhaustive)
    add_test(NAME ofg-terrain-randomized COMMAND ofg-terrain-test --test-suite=terrain-randomized)
    set_tests_properties(ofg-terrain-randomized PROPERTIES LABELS "cpu;terrain;randomized" TIMEOUT 900)
    set_tests_properties(ofg-terrain-fast PROPERTIES LABELS "cpu;terrain" TIMEOUT 60)
    set_tests_properties(ofg-terrain-exhaustive PROPERTIES LABELS "cpu;terrain;exhaustive" TIMEOUT 300)
    add_executable(ofg-core-test
        tests/outdoor-lighting-test.cpp
        tests/gltf-test.cpp
        tests/animation-test.cpp
        tests/model-resources-test.cpp
        tests/render-settings-test.cpp
        tests/pbr-core-test.cpp
        tests/texture-test.cpp
        tests/core-test-main.cpp
        tests/math-test.cpp
        tests/object-ptr-test.cpp
        tests/resources-test.cpp
        tests/scene-test.cpp
        tests/scene-rendering-test.cpp
        tests/state-machine-test.cpp
    )
    target_link_libraries(ofg-core-test PRIVATE ofg-core doctest)
    target_include_directories(ofg-core-test SYSTEM PRIVATE external/tinygltf)
    add_test(NAME ofg-core COMMAND ofg-core-test)
    set_tests_properties(ofg-core PROPERTIES LABELS "cpu" TIMEOUT 30)
endif()

if(NOT OFG_BUILD_APP)
    return()
endif()

# Embed the shared shader so launching from another directory needs no asset-path setup.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/checkerboard.slang)
file(READ shaders/checkerboard.slang CHECKERBOARD_SHADER)
configure_file(src/checkerboard-shader.h.in generated/checkerboard-shader.h @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/mesh.slang)
file(READ shaders/mesh.slang MESH_SHADER)
configure_file(src/lab/mesh-shader.h.in generated/mesh-shader.h @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/present.slang)
file(READ shaders/present.slang PRESENT_SHADER)
configure_file(src/render/present-shader.h.in generated/present-shader.h @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/mipmaps.slang)
file(READ shaders/mipmaps.slang MIPMAPS_SHADER)
configure_file(src/render/mipmaps-shader.h.in generated/mipmaps-shader.h @ONLY)

file(READ shaders/pbr/common.slang PBR_OUTPUT_SHADER)
file(READ shaders/pbr/output.slang OUTPUT_SOURCE)
string(APPEND PBR_OUTPUT_SHADER "\n${OUTPUT_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/pbr/output.slang)
configure_file(src/render/pbr-output-shader.h.in generated/pbr-output-shader.h @ONLY)

set(SKY_SHADER "")
foreach(module atmosphere clouds atmosphere-luts aerial-perspective sky)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky/${module}.slang")
    file(READ shaders/sky/${module}.slang source)
    string(APPEND SKY_SHADER "\n${source}\n")
endforeach()
configure_file(src/render/sky-shader.h.in generated/sky-shader.h @ONLY)
file(READ shaders/environment/filter.slang ENVIRONMENT_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/environment/filter.slang)
configure_file(src/render/environment-shader.h.in generated/environment-shader.h @ONLY)
file(READ shaders/shadows/ofg_shadow_pass.slang SHADOW_PASS_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/shadows/ofg_shadow_pass.slang)
file(READ shaders/shadows/depth.slang SHADOW_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/shadows/depth.slang)
configure_file(src/render/shadow-shader.h.in generated/shadow-shader.h @ONLY)
add_library(ofg-render STATIC
    src/lab/terrain-laboratory.cpp
    src/render/queue-completion.cpp
    src/render/shadow-renderer.cpp
    src/render/sky-renderer.cpp
    src/render/lighting-pass.cpp
    src/render/environment-renderer.cpp
    src/render/texture-renderer.cpp
    src/checkerboard.cpp
    src/render/graphics.cpp
    src/render/present.cpp
    src/game.cpp
    src/lab/scene-fixture.cpp
)
target_sources(ofg-render PRIVATE src/lab/outdoor-fixture.cpp src/lab/pbr-fixture.cpp src/lab/model-fixture.cpp)
file(READ shaders/deformation.slang DEFORMATION_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/deformation.slang)
configure_file(src/render/deformation-shader.h.in generated/deformation-shader.h @ONLY)
target_sources(ofg-render PRIVATE src/render/deformation.cpp)
target_sources(ofg-render PRIVATE src/lab/character-fixture.cpp)
target_compile_features(ofg-render PUBLIC cxx_std_20)
target_include_directories(ofg-render PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ofg-render PUBLIC slang-rhi ofg-core)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/imgui.slang)
file(READ shaders/imgui.slang IMGUI_SHADER)
configure_file(src/ui/imgui-shader.h.in generated/imgui-shader.h @ONLY)
add_library(ofg-ui STATIC src/ui/terrain-panel.cpp src/ui/render-settings-panel.cpp src/ui/imgui-renderer.cpp src/ui/workspace.cpp src/ui/animation-panel.cpp)
target_include_directories(ofg-ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ofg-ui PUBLIC ofg-render imgui)

# Small shared-backend compute-to-vertex regression, separate from application skinning.
file(READ shaders/compute-vertex-proof.slang COMPUTE_VERTEX_PROOF_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/compute-vertex-proof.slang)
configure_file(tests/compute-vertex-proof-shader.h.in generated/compute-vertex-proof-shader.h @ONLY)
add_library(ofg-compute-proof STATIC tests/compute-vertex-proof.cpp)
target_include_directories(ofg-compute-proof PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ofg-compute-proof PUBLIC ofg-render)

if(EMSCRIPTEN)
    target_compile_options(ofg-render PRIVATE --use-port=emdawnwebgpu)
    target_sources(ofg-ui PRIVATE src/ui/browser-input.cpp)
    add_executable(ofg-terrain-workers-proof tests/terrain-workers-web.cpp)
    target_link_libraries(ofg-terrain-workers-proof PRIVATE ofg-core)
    set_target_properties(ofg-terrain-workers-proof PROPERTIES OUTPUT_NAME terrain-workers-proof SUFFIX .js)
    # The lifecycle proof deliberately overlaps an old draining pair with its replacement pair.
    target_link_options(ofg-terrain-workers-proof PRIVATE
        -fwasm-exceptions -sPTHREAD_POOL_SIZE=6 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
        -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sALLOW_BLOCKING_ON_MAIN_THREAD=0
        -sASYNCIFY=1 -sASYNCIFY_IGNORE_INDIRECT=1
        "-sASYNCIFY_ONLY=['*main*','*wait*','*create*','*map*','*ofg*','*dynCall*']")
    add_executable(ofg-web src/web-main.cpp)
    add_dependencies(ofg-web ofg-terrain-workers-proof)
    target_link_libraries(ofg-web PRIVATE ofg-ui ofg-core)
    set_target_properties(ofg-web PROPERTIES OUTPUT_NAME index SUFFIX .html)
    set_property(TARGET ofg-web APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # WebGPU initialization, initial buffer uploads and uniform staging maps yield through Asyncify.
    # Cover OFG/RHI call chains and Emscripten callback thunks, excluding the embedded Slang compiler.
    # Emscripten pauses/resumes its main loop while a frame is suspended.
    target_link_options(ofg-web PRIVATE
        --use-port=emdawnwebgpu
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
        -fwasm-exceptions --export=__cpp_exception
        -sFETCH=1 -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
        -sPTHREAD_POOL_SIZE=5 -sALLOW_BLOCKING_ON_MAIN_THREAD=0
        -sASYNCIFY=1 -sASYNCIFY_IGNORE_INDIRECT=1
        "-sASYNCIFY_ONLY=['*main*','*wait*','*initialize*','*create*','*map*','*readBuffer*','*ofg*','*rhi*','*Frame*','*dynCall*']"
    )
    add_executable(ofg-compute-proof-web tests/compute-vertex-web.cpp)
    target_link_libraries(ofg-compute-proof-web PRIVATE ofg-compute-proof)
    set_target_properties(ofg-compute-proof-web PROPERTIES OUTPUT_NAME compute-proof SUFFIX .html)
    set_property(TARGET ofg-compute-proof-web APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/compute-vertex-shell.html)
    target_link_options(ofg-compute-proof-web PRIVATE
        --use-port=emdawnwebgpu --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/compute-vertex-shell.html
        -fwasm-exceptions --export=__cpp_exception
        -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
        -sASYNCIFY=1 -sASYNCIFY_IGNORE_INDIRECT=1
        "-sASYNCIFY_ONLY=['*main*','*frame*','*wait*','*initialize*','*create*','*map*','*readBuffer*','*ofg*','*rhi*','*dynCall*']"
    )
else()
    add_executable(ofg src/main.cpp)
    target_link_libraries(ofg PRIVATE ofg-ui ofg-core glfw)
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_test(NAME ofg-startup COMMAND ofg --check-device)

    add_executable(ofg-render-test tests/outdoor-rendering-test.cpp tests/checkerboard-test.cpp tests/graphics-test.cpp tests/compute-vertex-test.cpp tests/rhi-bindings-test.cpp)
    target_link_libraries(ofg-render-test PRIVATE ofg-ui ofg-compute-proof doctest)
    target_compile_definitions(ofg-render-test PRIVATE OFG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ofg-checkerboard COMMAND ofg-render-test "--test-case=Checkerboard*")
    add_test(NAME ofg-scene-rendering COMMAND ofg-render-test "--test-case-exclude=Checkerboard*")
    set_tests_properties(ofg-startup ofg-checkerboard PROPERTIES LABELS "native;gpu" TIMEOUT 30)
    # The complete validated-device/shader suite measured 139 seconds in Release. Individual async
    # tests retain their own deadlines; this allowance covers the aggregate suite and device setup.
    set_tests_properties(ofg-scene-rendering PROPERTIES LABELS "native;gpu" TIMEOUT 300)
endif()
