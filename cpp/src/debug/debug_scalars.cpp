// Debug scalar wrapper objects used by global debug variable declarations.
//
// These wrappers keep local scalar storage simple while delegating registration,
// lookup, diagnostics, and menu-tree caching to DebugMenu.
#include "ofg/debug/debug_menu.hpp"

namespace ofg {
namespace {

constexpr std::uint64_t k_no_registration_id = 0;

} // namespace

DebugBool::DebugBool(const char* path, bool default_value) noexcept
    : m_path(path == nullptr ? std::string_view{} : std::string_view{path}), m_value(default_value),
      m_default_value(default_value) {
    try {
        const DebugMenu::RegistrationResult registration =
            DebugMenu::instance().register_bool(*this, m_path, default_value);
        m_registered = registration.m_registered;
        m_registration_id = registration.m_id;
    } catch (...) {
        m_registered = false;
        m_registration_id = k_no_registration_id;
    }
}

DebugBool::~DebugBool() noexcept {
    if (m_registered) {
        DebugMenu::instance().unregister_bool(m_registration_id);
    }
}

DebugBool::operator bool() const noexcept {
    return m_value;
}

DebugBool& DebugBool::operator=(bool value) noexcept {
    set(value);
    return *this;
}

bool DebugBool::value() const noexcept {
    return m_value;
}

bool DebugBool::default_value() const noexcept {
    return m_default_value;
}

std::string_view DebugBool::path() const noexcept {
    return m_path;
}

bool DebugBool::registered() const noexcept {
    return m_registered;
}

void DebugBool::set(bool value) noexcept {
    m_value = value;
}

DebugInt::DebugInt(const char* path, int default_value) noexcept
    : m_path(path == nullptr ? std::string_view{} : std::string_view{path}), m_value(default_value),
      m_default_value(default_value) {
    try {
        const DebugMenu::RegistrationResult registration =
            DebugMenu::instance().register_int(*this, m_path, default_value);
        m_registered = registration.m_registered;
        m_registration_id = registration.m_id;
    } catch (...) {
        m_registered = false;
        m_registration_id = k_no_registration_id;
    }
}

DebugInt::~DebugInt() noexcept {
    if (m_registered) {
        DebugMenu::instance().unregister_int(m_registration_id);
    }
}

DebugInt::operator int() const noexcept {
    return m_value;
}

DebugInt& DebugInt::operator=(int value) noexcept {
    set(value);
    return *this;
}

int DebugInt::value() const noexcept {
    return m_value;
}

int DebugInt::default_value() const noexcept {
    return m_default_value;
}

std::string_view DebugInt::path() const noexcept {
    return m_path;
}

bool DebugInt::registered() const noexcept {
    return m_registered;
}

void DebugInt::set(int value) noexcept {
    m_value = value;
}

DebugFloat::DebugFloat(const char* path, float default_value) noexcept
    : m_path(path == nullptr ? std::string_view{} : std::string_view{path}), m_value(default_value),
      m_default_value(default_value) {
    try {
        const DebugMenu::RegistrationResult registration =
            DebugMenu::instance().register_float(*this, m_path, default_value);
        m_registered = registration.m_registered;
        m_registration_id = registration.m_id;
    } catch (...) {
        m_registered = false;
        m_registration_id = k_no_registration_id;
    }
}

DebugFloat::~DebugFloat() noexcept {
    if (m_registered) {
        DebugMenu::instance().unregister_float(m_registration_id);
    }
}

DebugFloat::operator float() const noexcept {
    return m_value;
}

DebugFloat& DebugFloat::operator=(float value) noexcept {
    set(value);
    return *this;
}

float DebugFloat::value() const noexcept {
    return m_value;
}

float DebugFloat::default_value() const noexcept {
    return m_default_value;
}

std::string_view DebugFloat::path() const noexcept {
    return m_path;
}

bool DebugFloat::registered() const noexcept {
    return m_registered;
}

void DebugFloat::set(float value) noexcept {
    m_value = value;
}

} // namespace ofg
