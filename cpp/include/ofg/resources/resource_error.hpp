// Resource validation error helpers for OFG renderer resources.
//
// Public factories still report failures through std::string& to match the
// existing C++ style, while these small codes give tests and future callers a
// stable vocabulary for common validation categories.
#pragma once

#include <string>

namespace ofg {

enum class ResourceErrorCode {
    InvalidArgument,
    MissingProperty,
    TypeMismatch,
    OutOfRange,
};

struct ResourceError {
    ResourceErrorCode m_code{ResourceErrorCode::InvalidArgument};
    std::string m_message;
};

// Converts a resource error code into a stable diagnostic label.
[[nodiscard]] const char* resource_error_code_name(ResourceErrorCode code) noexcept;

// Builds a typed resource error value.
[[nodiscard]] ResourceError make_resource_error(ResourceErrorCode code, std::string message);

} // namespace ofg
