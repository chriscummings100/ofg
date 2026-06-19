// Runs Rust coverage for unit tests plus the native render-smoke binary.

import {
  mkdirSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync
} from "node:fs";
import { extname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const coverageDir = resolve(root, "artifacts/coverage/rust");
const summaryPath = resolve(coverageDir, "summary.json");
const prettySummaryPath = resolve(coverageDir, "summary.pretty.json");
const threshold = 90;
const lineCoverageExceptions = new Map([
  [
    normalizePath("crates/ofg_test_harness/src/bin/ofg-render-frame.rs"),
    "exercised by instrumented native smoke; remaining uncovered lines are failure handling"
  ]
]);
const omittedCoverageExceptions = new Map([
  [
    normalizePath("crates/ofg_web/src/browser.rs"),
    "wasm32-only browser WebGPU facade; covered by test:wasm and smoke:browser"
  ]
]);

ensureCargoLlvmCov();
rmSync(coverageDir, { recursive: true, force: true });
mkdirSync(coverageDir, { recursive: true });

run("cargo", ["llvm-cov", "clean", "--workspace"]);
run("cargo", ["llvm-cov", "test", "--workspace", "--no-report"]);
run("cargo", [
  "llvm-cov",
  "run",
  "--package",
  "ofg_test_harness",
  "--bin",
  "ofg-render-frame",
  "--no-report",
  "--",
  "--out",
  "artifacts/render-smoke-coverage"
]);
run("cargo", [
  "llvm-cov",
  "report",
  "--json",
  "--summary-only",
  "--output-path",
  summaryPath
]);

const summary = JSON.parse(readFileSync(summaryPath, "utf8"));
writeFileSync(prettySummaryPath, `${JSON.stringify(summary, null, 2)}\n`);
const { failures, missing } = collectCoverageFailures(summary);
if (missing.length > 0) {
  console.error("Rust implementation files missing from coverage summary:");
  for (const path of missing) {
    console.error(`- ${path}`);
  }
  process.exit(1);
}
if (failures.length > 0) {
  console.error("Rust files below coverage threshold:");
  for (const failure of failures) {
    console.error(
      `- ${failure.path}: ${failure.percent.toFixed(2)}% lines (${failure.covered}/${failure.total})`
    );
  }
  process.exit(1);
}
console.log(`Rust coverage passed for checked files at >= ${threshold}% line coverage.`);
for (const [path, reason] of lineCoverageExceptions) {
  console.log(`Rust coverage exception: ${path} (${reason})`);
}
for (const [path, reason] of omittedCoverageExceptions) {
  console.log(`Rust coverage omitted-file exception: ${path} (${reason})`);
}

function ensureCargoLlvmCov() {
  const result = spawnSync("cargo", ["llvm-cov", "--version"], {
    cwd: root,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"]
  });
  if (result.status !== 0) {
    throw new Error(
      "cargo-llvm-cov is required for Rust coverage. Install it with: cargo install cargo-llvm-cov --locked"
    );
  }
}

function collectCoverageFailures(summary) {
  const files = summary?.data?.[0]?.files;
  if (!Array.isArray(files)) {
    throw new Error("Unexpected cargo-llvm-cov summary shape.");
  }

  const seenFiles = new Set();
  const failures = [];
  for (const file of files) {
    const relativePath = workspaceRelativePath(file.filename);
    if (relativePath === null) {
      continue;
    }
    seenFiles.add(relativePath);
    if (lineCoverageExceptions.has(relativePath)) {
      continue;
    }

    const lines = file.summary?.lines;
    const percent = Number(lines?.percent);
    if (!Number.isFinite(percent)) {
      throw new Error(`Missing line coverage for ${file.filename}.`);
    }
    if (percent < threshold) {
      failures.push({
        path: relativePath,
        percent,
        covered: Number(lines.covered),
        total: Number(lines.count)
      });
    }
  }
  return {
    failures,
    missing: collectMissingImplementationFiles(seenFiles)
  };
}

function collectMissingImplementationFiles(seenFiles) {
  const missing = [];
  for (const file of rustImplementationFiles(resolve(root, "crates"))) {
    const relativePath = normalizePath(relative(root, file));
    if (seenFiles.has(relativePath) || omittedCoverageExceptions.has(relativePath)) {
      continue;
    }
    missing.push(relativePath);
  }
  return missing;
}

function rustImplementationFiles(dir) {
  const files = [];
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    const stats = statSync(path);
    if (stats.isDirectory()) {
      files.push(...rustImplementationFiles(path));
    } else if (stats.isFile() && extname(entry) === ".rs" && hasExecutableRust(path)) {
      files.push(path);
    }
  }
  return files;
}

function hasExecutableRust(path) {
  const source = readFileSync(path, "utf8");
  return /(^|\n)\s*(pub\s+)?(async\s+)?fn\s/.test(source) ||
    /(^|\n)\s*impl\s/.test(source) ||
    /(^|\n)\s*(pub\s+)?(struct|enum|const)\s/.test(source);
}

function workspaceRelativePath(path) {
  const relativePath = relative(root, resolve(path));
  if (
    relativePath === "" ||
    isAbsolute(relativePath) ||
    relativePath.startsWith(`..${sep}`) ||
    relativePath === ".."
  ) {
    return null;
  }
  return normalizePath(relativePath);
}

function normalizePath(path) {
  return path.replaceAll("\\", "/");
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: "inherit"
  });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
