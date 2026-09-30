# OFG terrain laboratory: project setup and external dependencies.
cmake_minimum_required(VERSION 3.24)
project(ofg LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OFG currently supports native Windows builds.")
endif()

add_subdirectory(external)

add_executable(ofg src/main.cpp)
target_compile_features(ofg PRIVATE cxx_std_20)
target_link_libraries(ofg PRIVATE slang-rhi)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ofg-startup COMMAND ofg)
    set_tests_properties(ofg-startup PROPERTIES LABELS "native;gpu")
endif()
