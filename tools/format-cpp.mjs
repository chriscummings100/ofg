// Formats or checks OFG C++ source files with clang-format.
//
// The wrapper mirrors the rest of the local C++ tooling: it discovers installed
// LLVM tools from PATH, CLANG_FORMAT, or common Visual Studio locations, and it
// never depends on repository-local toolchain downloads.
import { readdirSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { findClangFormat, run } from "./lib/toolchain.mjs";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const clangFormat = findClangFormat(rootDir);
const checkOnly = process.argv.includes("--check");
const files = collectCppFiles([
  path.join(rootDir, "cpp", "include"),
  path.join(rootDir, "cpp", "src"),
  path.join(rootDir, "cpp", "tests")
]);

if (files.length === 0) {
  throw new Error("No C++ files found to format.");
}

const args = checkOnly
  ? ["--dry-run", "--Werror", "--style=file", ...files]
  : ["-i", "--style=file", ...files];

run(clangFormat, args, { cwd: rootDir });
console.log(`${checkOnly ? "Checked" : "Formatted"} ${files.length} C++ files with ${clangFormat}.`);

// Recursively collects project C++ files while avoiding third-party/generated code.
function collectCppFiles(roots) {
  const extensions = new Set([".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"]);
  const results = [];
  for (const root of roots) {
    collect(root, extensions, results);
  }
  return results.sort((left, right) => left.localeCompare(right));
}

// Walks a directory tree and appends files with known C++ extensions.
function collect(directory, extensions, results) {
  if (!statSync(directory, { throwIfNoEntry: false })?.isDirectory()) {
    return;
  }

  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const fullPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      collect(fullPath, extensions, results);
    } else if (entry.isFile() && extensions.has(path.extname(entry.name))) {
      results.push(fullPath);
    }
  }
}
