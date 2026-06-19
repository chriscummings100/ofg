// Runs TypeScript coverage through c8 and enforces per-source line thresholds.

import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { isAbsolute, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const coverageDir = resolve(root, "artifacts/coverage/ts");
const summaryPath = resolve(coverageDir, "coverage-summary.json");
const threshold = 90;
const lineCoverageExceptions = new Map([
  [
    normalizePath("src/app/main.ts"),
    "browser entrypoint is exercised by smoke:browser rather than Node unit tests"
  ]
]);

rmSync(coverageDir, { recursive: true, force: true });
mkdirSync(coverageDir, { recursive: true });

run("node", ["tools/clean-dist.mjs"]);
run("node", ["tools/build-wasm.mjs"]);
run("node", ["./node_modules/typescript/bin/tsc", "-p", "tsconfig.app.json"]);
run("node", ["./node_modules/typescript/bin/tsc", "-p", "tsconfig.test.json"]);
run("node", [
  "./node_modules/c8/bin/c8.js",
  "--all",
  "--include",
  "dist-test/src/app/**/*.js",
  "--reporter=json-summary",
  "--reporter=text-summary",
  "--reports-dir",
  "artifacts/coverage/ts",
  "node",
  "--import",
  "./dist-test/tests/ts/setupDom.js",
  "./node_modules/mocha/bin/mocha.js",
  "dist-test/tests/ts/**/*.test.js"
]);

const summary = JSON.parse(readFileSync(summaryPath, "utf8"));
writeFileSync(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
rmSync(resolve(coverageDir, "tmp"), { recursive: true, force: true });
const failures = collectCoverageFailures(summary);
if (failures.length > 0) {
  console.error("TypeScript files below coverage threshold:");
  for (const failure of failures) {
    console.error(
      `- ${failure.path}: ${failure.percent.toFixed(2)}% lines (${failure.covered}/${failure.total})`
    );
  }
  process.exit(1);
}
console.log(`TypeScript coverage passed for checked files at >= ${threshold}% line coverage.`);
for (const [path, reason] of lineCoverageExceptions) {
  console.log(`TypeScript coverage exception: ${path} (${reason})`);
}

function collectCoverageFailures(summary) {
  const failures = [];
  for (const [filename, fileSummary] of Object.entries(summary)) {
    if (filename === "total") {
      continue;
    }

    const relativePath = workspaceRelativePath(filename);
    if (relativePath === null || lineCoverageExceptions.has(relativePath)) {
      continue;
    }

    const lines = fileSummary.lines;
    const percent = Number(lines?.pct);
    if (!Number.isFinite(percent)) {
      throw new Error(`Missing line coverage for ${filename}.`);
    }
    if (percent < threshold) {
      failures.push({
        path: relativePath,
        percent,
        covered: Number(lines.covered),
        total: Number(lines.total)
      });
    }
  }
  return failures;
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
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
