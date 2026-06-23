// Resource validation error helpers for OFG renderer resources.
#include "ofg/resources/resource_error.hpp"

#include <string>
#include <utility>

namespace ofg {

// Converts a resource error code into a stable diagnostic label.
const char* resource_error_code_name(ResourceErrorCode code) noexcept {
    switch (code) {
    case ResourceErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ResourceErrorCode::MissingProperty:
        return "MissingProperty";
    case ResourceErrorCode::TypeMismatch:
        return "TypeMismatch";
    case ResourceErrorCode::OutOfRange:
        return "OutOfRange";
    }
    return "Unknown";
}

// Builds a typed resource error value.
ResourceError make_resource_error(ResourceErrorCode code, std::string message) {
    return ResourceError{code, std::move(message)};
}

} // namespace ofg
