"""Seed eight controller faults in an isolated build and require the production contract tests to reject each."""
from pathlib import Path
import json
import subprocess

root = Path(__file__).resolve().parents[1]
folder = root / "artifacts/terrain/mutations"
folder.mkdir(parents=True, exist_ok=True)
source = (root / "src/terrain/terrain-stream.cpp").read_text()
mutant = folder / "terrain-stream.cpp"
mutant.write_text(source)
(folder / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.24)
project(terrain_mutations LANGUAGES CXX)
add_executable(terrain-mutant "{root.as_posix()}/tests/core-test-main.cpp"
    "{root.as_posix()}/tests/terrain-stream-test.cpp" terrain-stream.cpp
    "{root.as_posix()}/src/terrain/terrain-address.cpp")
target_compile_features(terrain-mutant PRIVATE cxx_std_20)
target_include_directories(terrain-mutant PRIVATE "{root.as_posix()}/src" "{root.as_posix()}/external/doctest")
''')


def command(arguments, name):
    """Save full command output; return its real process status rather than treating compilation failure as detection."""
    result = subprocess.run(arguments, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (folder / f"{name}.log").write_text(result.stdout)
    return result.returncode


def replace(text, old, new):
    """Require a unique mutation site so source changes fail usefully instead of silently testing the wrong defect."""
    if text.count(old) != 1:
        raise RuntimeError(f"Mutation site must match once: {old!r} ({text.count(old)} matches)")
    return text.replace(old, new, 1)


faults = {
    "seven-children": [("if (slot(key).state != BuildState::Loaded)",
                         "if (slot(key).state != BuildState::Loaded && !(key.depth && (key.x & 1) && (key.y & 1) && (key.z & 1)))")],
    "cpu-ready-admission": [("job.generated = true;", "job.generated = true; content->state = BuildState::Loaded; content->payload = std::make_shared<PreparedPayload>();")],
    "address-only-owner": [("job == m_jobs.end() || job->second.cancelled", "job == m_jobs.end()"),
                           ("!node || node->content.request != id || node->content.state != BuildState::Loading", "!node")],
    "early-cancel-release": [("result.reservedCpuBytes += job.request.byteLimit;", "if (job.cancelled) continue; result.reservedCpuBytes += job.request.byteLimit;")],
    "early-parent-removal": [("m_dispatch.push_back(request);", "m_dispatch.push_back(request); std::erase_if(m_cut, [&](const auto& e) { return isAncestor(e.address, request.address); });")],
    "stale-seams": [("mask |= uint8_t(1u << face);", "mask |= 0;")],
    "submission-retirement": [("payload->lastSubmission <= serial && payload.use_count() == 1", "payload.use_count() == 1")],
    "partial-group-reservation": [("m_budgetBlocked = true;\n            continue;\n        }\n        missing.insert", "m_budgetBlocked = true;\n            missing.push_back(group.front());\n            continue;\n        }\n        missing.insert")],
}
report = {}
try:
    assert command(["cmake", "-S", str(folder), "-B", str(folder / "build"), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"], "configure") == 0
    for name, changes in faults.items():
        text = source
        for old, new in changes:
            text = replace(text, old, new)
        mutant.write_text(text)
        assert command(["cmake", "--build", str(folder / "build"), "--parallel", "4"], f"{name}-build") == 0, f"{name} did not compile"
        code = command([str(folder / "build/terrain-mutant.exe"), "--test-suite=terrain-fast", "--abort-after=3"], name)
        report[name] = {"compiled": True, "exitCode": code, "detected": code != 0}
        print(f"{name}: {'detected' if code else 'SURVIVED'}", flush=True)
        assert code != 0, f"Tests failed to detect {name}"
finally:
    mutant.write_text(source)
    (folder / "report.json").write_text(json.dumps(report, indent=2))
assert command(["cmake", "--build", str(folder / "build"), "--parallel", "4"], "restored-build") == 0
assert command([str(folder / "build/terrain-mutant.exe"), "--test-suite=terrain-fast"], "restored") == 0
print("All eight faults detected; unmodified controller passes.")
