// Doctest coverage for the GPU-free debug scalar registry.
//
// The debug menu is the data model that future ImGui and browser facades edit.
// These tests keep path registration, typed lookup, diagnostics, and cached tree
// rebuilding independent from any renderer or Dear ImGui context.
#include "doctest.h"

#include "ofg/debug/debug_menu.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

const ofg::DebugMenuDiagnostic* find_diagnostic(std::string_view path,
    ofg::DebugMenuDiagnosticKind kind,
    ofg::DebugScalarType type,
    std::span<const ofg::DebugMenuDiagnostic> diagnostics) {
    for (const ofg::DebugMenuDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.m_path == path && diagnostic.m_kind == kind && diagnostic.m_attempted_type == type) {
            return &diagnostic;
        }
    }
    return nullptr;
}

const ofg::DebugMenuTreeNode* find_child(const std::vector<ofg::DebugMenuTreeNode>& nodes, std::string_view label) {
    for (const ofg::DebugMenuTreeNode& node : nodes) {
        if (node.m_label == label) {
            return &node;
        }
    }
    return nullptr;
}

const ofg::DebugMenuTreeNode* find_node(const ofg::DebugMenuTree& tree, std::span<const std::string_view> labels) {
    const std::vector<ofg::DebugMenuTreeNode>* nodes = &tree.m_nodes;
    const ofg::DebugMenuTreeNode* current = nullptr;
    for (std::string_view label : labels) {
        current = find_child(*nodes, label);
        if (current == nullptr) {
            return nullptr;
        }
        nodes = &current->m_children;
    }
    return current;
}

bool tree_has_entry(const ofg::DebugMenuTree& tree, std::string_view path) {
    for (const ofg::DebugMenuTreeEntry& entry : tree.m_entries) {
        if (entry.m_path == path) {
            return true;
        }
    }

    std::vector<const ofg::DebugMenuTreeNode*> stack;
    for (const ofg::DebugMenuTreeNode& node : tree.m_nodes) {
        stack.push_back(&node);
    }
    while (!stack.empty()) {
        const ofg::DebugMenuTreeNode* node = stack.back();
        stack.pop_back();
        for (const ofg::DebugMenuTreeEntry& entry : node->m_entries) {
            if (entry.m_path == path) {
                return true;
            }
        }
        for (const ofg::DebugMenuTreeNode& child : node->m_children) {
            stack.push_back(&child);
        }
    }
    return false;
}

template <typename Entry> bool entries_have_path(std::span<const Entry> entries, std::string_view path) {
    for (const Entry& entry : entries) {
        if (entry.m_path == path) {
            return true;
        }
    }
    return false;
}

} // namespace

// Verifies public enum names remain stable for diagnostics and UI labels.
TEST_CASE("DebugMenu exposes stable type and diagnostic names") {
    CHECK(std::string{ofg::debug_scalar_type_name(ofg::DebugScalarType::Bool)} == "bool");
    CHECK(std::string{ofg::debug_scalar_type_name(ofg::DebugScalarType::Int)} == "int");
    CHECK(std::string{ofg::debug_scalar_type_name(ofg::DebugScalarType::Float)} == "float");
    CHECK(
        std::string{ofg::debug_menu_diagnostic_kind_name(ofg::DebugMenuDiagnosticKind::InvalidPath)} == "invalid_path");
    CHECK(std::string{ofg::debug_menu_diagnostic_kind_name(ofg::DebugMenuDiagnosticKind::DuplicatePath)} ==
          "duplicate_path");
}

// Verifies all scalar wrappers register, cast naturally, and share typed get/set APIs.
TEST_CASE("DebugMenu registers bool int and float scalars") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::string bool_path = "tests/debug_menu/scalars/bool";
    const std::string int_path = "tests/debug_menu/scalars/int";
    const std::string float_path = "tests/debug_menu/scalars/float";

    {
        ofg::DebugBool flag(bool_path.c_str(), true);
        ofg::DebugInt count(int_path.c_str(), 7);
        ofg::DebugFloat scale(float_path.c_str(), 1.5f);

        CHECK(flag.registered());
        CHECK(count.registered());
        CHECK(scale.registered());
        CHECK(flag.default_value());
        CHECK(count.default_value() == 7);
        CHECK(scale.default_value() == doctest::Approx(1.5f));
        CHECK(std::string{flag.path()} == bool_path);
        CHECK(std::string{count.path()} == int_path);
        CHECK(std::string{scale.path()} == float_path);
        CHECK(entries_have_path(menu.bool_entries(), bool_path));
        CHECK(entries_have_path(menu.int_entries(), int_path));
        CHECK(entries_have_path(menu.float_entries(), float_path));
        CHECK(static_cast<bool>(flag));
        CHECK(static_cast<int>(count) == 7);
        CHECK(static_cast<float>(scale) == doctest::Approx(1.5f));

        flag = false;
        count = 11;
        scale = 2.25f;
        CHECK(menu.get_bool(bool_path).value_or(true) == false);
        CHECK(menu.get_int(int_path).value_or(0) == 11);
        CHECK(menu.get_float(float_path).value_or(0.0f) == doctest::Approx(2.25f));

        CHECK(menu.set_bool(bool_path, true));
        CHECK(menu.set_int(int_path, 13));
        CHECK(menu.set_float(float_path, 3.5f));
        CHECK(flag.value());
        CHECK(count.value() == 13);
        CHECK(scale.value() == doctest::Approx(3.5f));

        CHECK_FALSE(menu.get_bool(int_path).has_value());
        CHECK_FALSE(menu.get_int(float_path).has_value());
        CHECK_FALSE(menu.get_float(bool_path).has_value());
        CHECK_FALSE(menu.set_bool(float_path, true));
        CHECK_FALSE(menu.set_int(bool_path, 1));
        CHECK_FALSE(menu.set_float(int_path, 1.0f));
    }

    CHECK_FALSE(menu.get_bool(bool_path).has_value());
    CHECK_FALSE(menu.get_int(int_path).has_value());
    CHECK_FALSE(menu.get_float(float_path).has_value());
}

// Verifies declaration macros create ordinary global-style scalar objects.
TEST_CASE("DEBUG scalar macros create named wrappers") {
    const std::string bool_path = "tests/debug_menu/macros/bool";
    const std::string int_path = "tests/debug_menu/macros/int";
    const std::string float_path = "tests/debug_menu/macros/float";

    DEBUG_BOOL(bool_path.c_str(), macro_bool, false);
    DEBUG_INT(int_path.c_str(), macro_int, 4);
    DEBUG_FLOAT(float_path.c_str(), macro_float, 0.75f);

    CHECK(macro_bool.registered());
    CHECK(macro_int.registered());
    CHECK(macro_float.registered());
    CHECK(static_cast<bool>(macro_bool) == false);
    CHECK(static_cast<int>(macro_int) == 4);
    CHECK(static_cast<float>(macro_float) == doctest::Approx(0.75f));
}

// Verifies invalid paths produce inert wrappers and stable diagnostics instead of exceptions.
TEST_CASE("DebugMenu records invalid path diagnostics") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::size_t diagnostic_count_before = menu.diagnostics().size();

    ofg::DebugBool empty_path("", true);
    ofg::DebugInt leading_slash("/tests/debug_menu/invalid", 1);
    ofg::DebugInt trailing_slash("tests/debug_menu/invalid/", 2);
    ofg::DebugFloat double_slash("tests/debug_menu//invalid", 1.0f);
    ofg::DebugFloat control_character("tests/debug_menu/invalid/\t", 2.0f);
    ofg::DebugBool non_ascii("tests/debug_menu/invalid/\xC2\xA3", false);

    CHECK_FALSE(empty_path.registered());
    CHECK_FALSE(leading_slash.registered());
    CHECK_FALSE(trailing_slash.registered());
    CHECK_FALSE(double_slash.registered());
    CHECK_FALSE(control_character.registered());
    CHECK_FALSE(non_ascii.registered());
    CHECK(empty_path.value());
    CHECK(leading_slash.value() == 1);
    CHECK(trailing_slash.value() == 2);
    CHECK(double_slash.value() == doctest::Approx(1.0f));
    CHECK(control_character.value() == doctest::Approx(2.0f));
    CHECK_FALSE(non_ascii.value());
    CHECK(menu.diagnostics().size() >= diagnostic_count_before + 6U);

    CHECK(
        find_diagnostic(
            "", ofg::DebugMenuDiagnosticKind::InvalidPath, ofg::DebugScalarType::Bool, menu.diagnostics()) != nullptr);
    CHECK(find_diagnostic("/tests/debug_menu/invalid",
              ofg::DebugMenuDiagnosticKind::InvalidPath,
              ofg::DebugScalarType::Int,
              menu.diagnostics()) != nullptr);
    CHECK(find_diagnostic("tests/debug_menu/invalid/",
              ofg::DebugMenuDiagnosticKind::InvalidPath,
              ofg::DebugScalarType::Int,
              menu.diagnostics()) != nullptr);
    CHECK(find_diagnostic("tests/debug_menu//invalid",
              ofg::DebugMenuDiagnosticKind::InvalidPath,
              ofg::DebugScalarType::Float,
              menu.diagnostics()) != nullptr);
}

// Verifies duplicate and cross-type duplicate paths keep the first variable live.
TEST_CASE("DebugMenu rejects duplicate paths without replacing the first scalar") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::string path = "tests/debug_menu/duplicates/shared";
    const std::size_t diagnostic_count_before = menu.diagnostics().size();

    ofg::DebugBool first(path.c_str(), true);
    ofg::DebugBool duplicate_bool(path.c_str(), false);
    ofg::DebugFloat duplicate(path.c_str(), 4.0f);

    CHECK(first.registered());
    CHECK_FALSE(duplicate_bool.registered());
    CHECK_FALSE(duplicate.registered());
    CHECK(menu.get_bool(path).value_or(false));
    CHECK_FALSE(menu.get_float(path).has_value());
    CHECK(menu.diagnostics().size() >= diagnostic_count_before + 2U);

    const ofg::DebugMenuDiagnostic* bool_diagnostic = find_diagnostic(
        path, ofg::DebugMenuDiagnosticKind::DuplicatePath, ofg::DebugScalarType::Bool, menu.diagnostics());
    REQUIRE(bool_diagnostic != nullptr);
    CHECK(bool_diagnostic->m_message.find("attempted bool") != std::string::npos);

    const ofg::DebugMenuDiagnostic* float_diagnostic = find_diagnostic(
        path, ofg::DebugMenuDiagnosticKind::DuplicatePath, ofg::DebugScalarType::Float, menu.diagnostics());
    REQUIRE(float_diagnostic != nullptr);
    CHECK(float_diagnostic->m_message.find("already registered as bool") != std::string::npos);
}

// Verifies root-level entries and sorted sibling groups are present in the cached tree.
TEST_CASE("DebugMenu tree supports root entries and sorted sibling menus") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::string root_path = "tests_debug_menu_root_float";
    const std::string zeta_path = "tests/debug_menu/sort/zeta/value";
    const std::string alpha_path = "tests/debug_menu/sort/alpha/value";
    const std::string middle_path = "tests/debug_menu/sort/middle/value";

    ofg::DebugFloat root(root_path.c_str(), 9.0f);
    ofg::DebugBool zeta(zeta_path.c_str(), false);
    ofg::DebugInt alpha(alpha_path.c_str(), 1);
    ofg::DebugFloat middle(middle_path.c_str(), 2.0f);

    CHECK(root.registered());
    CHECK(zeta.registered());
    CHECK(alpha.registered());
    CHECK(middle.registered());
    CHECK(menu.refresh_tree_if_dirty());
    CHECK(tree_has_entry(menu.tree(), root_path));
    CHECK(tree_has_entry(menu.tree(), middle_path));

    const std::array<std::string_view, 3> sort_path = {"tests", "debug_menu", "sort"};
    const ofg::DebugMenuTreeNode* sort_node = find_node(menu.tree(), sort_path);
    REQUIRE(sort_node != nullptr);
    REQUIRE(sort_node->m_children.size() == 3U);
    CHECK(sort_node->m_children[0].m_label == "alpha");
    CHECK(sort_node->m_children[1].m_label == "middle");
    CHECK(sort_node->m_children[2].m_label == "zeta");
}

// Verifies late registrations under the same path prefix regroup only when the generation changes.
TEST_CASE("DebugMenu rebuilds grouped tree only when registry generation changes") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::string foo_path = "tests/debug_menu/tree_late/render/foo";
    const std::string bar_path = "tests/debug_menu/tree_late/render/bar";

    ofg::DebugBool foo(foo_path.c_str(), false);
    CHECK(foo.registered());
    CHECK(menu.refresh_tree_if_dirty());
    const std::uint64_t first_tree_generation = menu.tree().m_generation;
    CHECK(first_tree_generation == menu.registry_generation());
    CHECK_FALSE(menu.refresh_tree_if_dirty());

    ofg::DebugInt bar(bar_path.c_str(), 3);
    CHECK(bar.registered());
    CHECK(menu.registry_generation() > first_tree_generation);
    CHECK(menu.refresh_tree_if_dirty());
    CHECK(menu.tree().m_generation == menu.registry_generation());

    const std::array<std::string_view, 4> render_path = {"tests", "debug_menu", "tree_late", "render"};
    const ofg::DebugMenuTreeNode* render_node = find_node(menu.tree(), render_path);
    REQUIRE(render_node != nullptr);
    REQUIRE(render_node->m_entries.size() >= 2U);
    CHECK(render_node->m_entries[0].m_label == "bar");
    CHECK(render_node->m_entries[0].m_path == bar_path);
    CHECK(render_node->m_entries[0].m_type == ofg::DebugScalarType::Int);
    CHECK(render_node->m_entries[1].m_label == "foo");
    CHECK(render_node->m_entries[1].m_path == foo_path);
    CHECK(render_node->m_entries[1].m_type == ofg::DebugScalarType::Bool);
}

// Verifies scoped debug variables unregister and invalidate the cached tree.
TEST_CASE("DebugMenu unregisters scoped scalars") {
    ofg::DebugMenu& menu = ofg::DebugMenu::instance();
    const std::string path = "tests/debug_menu/unregister/scoped";
    std::uint64_t generation_with_entry = 0;

    {
        ofg::DebugBool scoped(path.c_str(), true);
        CHECK(scoped.registered());
        CHECK(menu.get_bool(path).has_value());
        CHECK(menu.refresh_tree_if_dirty());
        CHECK(tree_has_entry(menu.tree(), path));
        generation_with_entry = menu.registry_generation();
    }

    CHECK(menu.registry_generation() > generation_with_entry);
    CHECK_FALSE(menu.get_bool(path).has_value());
    CHECK(menu.refresh_tree_if_dirty());
    CHECK_FALSE(tree_has_entry(menu.tree(), path));
}
