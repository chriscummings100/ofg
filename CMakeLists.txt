# OFG terrain laboratory: project setup and external dependencies.
cmake_minimum_required(VERSION 3.24)
project(ofg LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OFG currently supports native Windows builds.")
endif()

add_subdirectory(external)

# Embed the shared shader so launching from another directory needs no asset-path setup.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/checkerboard.slang)
file(READ shaders/checkerboard.slang CHECKERBOARD_SHADER)
configure_file(src/checkerboard-shader.h.in generated/checkerboard-shader.h @ONLY)

add_library(ofg-render STATIC src/checkerboard.cpp)
target_compile_features(ofg-render PUBLIC cxx_std_20)
target_include_directories(ofg-render PUBLIC src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(ofg-render PUBLIC slang-rhi)

add_executable(ofg src/main.cpp)
target_link_libraries(ofg PRIVATE ofg-render glfw)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ofg-startup COMMAND ofg --check-device)

    add_executable(ofg-render-test tests/checkerboard-test.cpp)
    # Reuse the doctest header pinned by the RHI submodule for this GPU-dependent integration test.
    target_include_directories(ofg-render-test SYSTEM PRIVATE external/slang-rhi/external/doctest)
    target_link_libraries(ofg-render-test PRIVATE ofg-render)
    add_test(NAME ofg-checkerboard COMMAND ofg-render-test)
    set_tests_properties(ofg-startup ofg-checkerboard PROPERTIES LABELS "native;gpu" TIMEOUT 30)
endif()
