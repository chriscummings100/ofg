// Doctest coverage for typed OFG resource validation errors.
#include "doctest.h"

#include "ofg/resources/resource_error.hpp"

#include <string>

// Verifies resource error helpers expose stable diagnostic names.
TEST_CASE("resource error helper names cover every code") {
    CHECK(std::string(ofg::resource_error_code_name(ofg::ResourceErrorCode::InvalidArgument)) == "InvalidArgument");
    CHECK(std::string(ofg::resource_error_code_name(ofg::ResourceErrorCode::MissingProperty)) == "MissingProperty");
    CHECK(std::string(ofg::resource_error_code_name(ofg::ResourceErrorCode::TypeMismatch)) == "TypeMismatch");
    CHECK(std::string(ofg::resource_error_code_name(ofg::ResourceErrorCode::OutOfRange)) == "OutOfRange");

    const ofg::ResourceError error = ofg::make_resource_error(ofg::ResourceErrorCode::TypeMismatch, "bad type");
    CHECK(error.m_code == ofg::ResourceErrorCode::TypeMismatch);
    CHECK(error.m_message == "bad type");
}
