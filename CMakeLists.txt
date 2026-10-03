# OFG terrain laboratory: project setup and external dependencies.
cmake_minimum_required(VERSION 3.24)
project(ofg LANGUAGES C CXX)

option(OFG_BUILD_APP "Build the graphics application and GPU integration tests" ON)

if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions)
elseif(OFG_BUILD_APP AND NOT WIN32)
    message(FATAL_ERROR "OFG supports native Windows and Emscripten browser builds.")
endif()

if(NOT EMSCRIPTEN)
    include(CTest)
endif()
add_subdirectory(external)

add_library(ofg-core STATIC
    src/core/object.cpp
    src/math/mat.cpp
    src/math/quat.cpp
    src/math/transform.cpp
    src/resources/mesh.cpp
    src/resources/material.cpp
    src/resources/shader.cpp
    src/render/draw-list.cpp
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
target_include_directories(ofg-core PUBLIC src)

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_executable(ofg-core-test
        tests/core-test-main.cpp
        tests/math-test.cpp
        tests/object-ptr-test.cpp
        tests/resources-test.cpp
        tests/scene-test.cpp
        tests/scene-rendering-test.cpp
        tests/state-machine-test.cpp
    )
    target_link_libraries(ofg-core-test PRIVATE ofg-core doctest)
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

add_library(ofg-render STATIC src/checkerboard.cpp src/render/graphics.cpp src/render/present.cpp src/game.cpp src/lab/scene-fixture.cpp)
target_compile_features(ofg-render PUBLIC cxx_std_20)
target_include_directories(ofg-render PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ofg-render PUBLIC slang-rhi ofg-core)

if(EMSCRIPTEN)
    add_executable(ofg-web src/web-main.cpp)
    target_link_libraries(ofg-web PRIVATE ofg-render ofg-core)
    set_target_properties(ofg-web PROPERTIES OUTPUT_NAME index SUFFIX .html)
    set_property(TARGET ofg-web APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # WebGPU initialization, initial buffer uploads and uniform staging maps yield through Asyncify.
    # Cover OFG/RHI call chains and Emscripten callback thunks, excluding the embedded Slang compiler.
    # Emscripten pauses/resumes its main loop while a frame is suspended.
    target_link_options(ofg-web PRIVATE
        --use-port=emdawnwebgpu
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
        -fwasm-exceptions --export=__cpp_exception
        -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
        -sASYNCIFY=1 -sASYNCIFY_IGNORE_INDIRECT=1
        "-sASYNCIFY_ONLY=['*main*','*wait*','*initialize*','*create*','*map*','*readBuffer*','*ofg*','*rhi*','*Frame*','*dynCall*']"
    )
else()
    add_executable(ofg src/main.cpp)
    target_link_libraries(ofg PRIVATE ofg-render ofg-core glfw)
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_test(NAME ofg-startup COMMAND ofg --check-device)

    add_executable(ofg-render-test tests/checkerboard-test.cpp tests/graphics-test.cpp)
    target_link_libraries(ofg-render-test PRIVATE ofg-render doctest)
    add_test(NAME ofg-checkerboard COMMAND ofg-render-test "--test-case=Checkerboard*")
    add_test(NAME ofg-scene-rendering COMMAND ofg-render-test "--test-case-exclude=Checkerboard*")
    set_tests_properties(ofg-startup ofg-checkerboard ofg-scene-rendering PROPERTIES LABELS "native;gpu" TIMEOUT 30)
endif()
