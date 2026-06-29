// Common exception type for OFG engine failures.
//
// EngineError is thrown when C++ engine code detects invalid lifecycle use,
// invalid input, or lower-level GPU/resource failures. Browser and native
// boundaries catch it as std::exception and convert it to status/report data.
#pragma once

#include <stdexcept>
#include <string>

namespace ofg {

class EngineError : public std::runtime_error {
public:
    // Creates an engine exception with a human-readable diagnostic message.
    explicit EngineError(const std::string& message);
    // Creates an engine exception with a human-readable diagnostic message.
    explicit EngineError(const char* message);
};

inline EngineError::EngineError(const std::string& message) : std::runtime_error(message) {}

inline EngineError::EngineError(const char* message) : std::runtime_error(message) {}

} // namespace ofg
