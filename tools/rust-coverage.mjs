// Runs Rust workspace coverage through cargo-llvm-cov and writes local reports.
// The script is intentionally a wrapper, not an installer: coverage commands
// should be repeatable and should fail with setup guidance if tooling is absent.

import { spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const outputDir = "artifacts/coverage/rust";
const absoluteOutputDir = resolve(root, outputDir);
const textReportPath = `${outputDir}/coverage.txt`;
const summaryReportPath = `${outputDir}/summary.json`;
const prettySummaryReportPath = `${outputDir}/summary.pretty.json`;
const fullSummaryReportPath = `${outputDir}/summary.full.json`;
const fullPrettySummaryReportPath = `${outputDir}/summary.full.pretty.json`;
const absoluteTextReportPath = resolve(root, textReportPath);
const absoluteSummaryReportPath = resolve(root, summaryReportPath);
const absolutePrettySummaryReportPath = resolve(root, prettySummaryReportPath);
const absoluteFullSummaryReportPath = resolve(root, fullSummaryReportPath);
const absoluteFullPrettySummaryReportPath = resolve(root, fullPrettySummaryReportPath);
const args = process.argv.slice(2);
const passthroughIndex = args.indexOf("--");
const scriptArgs = passthroughIndex === -1 ? args : args.slice(0, passthroughIndex);
const passthroughArgs = passthroughIndex === -1 ? [] : args.slice(passthroughIndex + 1);
const defaultAttentionLineThreshold = 90;

if (scriptArgs.includes("--help")) {
  printUsage();
  process.exit();
}

const attentionLineThreshold = parseAttentionLineThreshold(scriptArgs);
const fullReport = scriptArgs.includes("--full");
const mirrorToolOutput = fullReport;
const writeHtml = scriptArgs.includes("--html");
const writeJson = scriptArgs.includes("--json");
const writeLcov = scriptArgs.includes("--lcov");
const noClean = scriptArgs.includes("--no-clean");
const showMissingLines = scriptArgs.includes("--show-missing-lines");

if (!hasCargoLlvmCov()) {
  printSetupGuidance();
  process.exitCode = 1;
  process.exit();
}

warnWhenLlvmToolsPreviewIsMissing();
mkdirSync(absoluteOutputDir, { recursive: true });

if (!noClean) {
  runCargoLlvmCov(["clean", "--workspace"], "clean previous Rust coverage state");
}

runCargoLlvmCov(
  [
    "--workspace",
    "--text",
    ...(showMissingLines ? ["--show-missing-lines"] : []),
    "--output-path",
    textReportPath,
    ...passthroughArgs
  ],
  "generate Rust text coverage report",
  { mirrorOutput: mirrorToolOutput }
);

runCargoLlvmCov(
  ["report", "--json", "--summary-only", "--output-path", fullSummaryReportPath],
  "generate Rust coverage full summary",
  { mirrorOutput: mirrorToolOutput }
);
writeCoverageSummary(
  absoluteFullSummaryReportPath,
  absoluteSummaryReportPath,
  {
    attentionLineThreshold,
    fullReport
  }
);
writePrettyJson(absoluteSummaryReportPath, absolutePrettySummaryReportPath);
writePrettyJson(absoluteFullSummaryReportPath, absoluteFullPrettySummaryReportPath);

if (writeJson) {
  runCargoLlvmCov(
    ["report", "--json", "--output-path", `${outputDir}/coverage.json`],
    "generate Rust JSON coverage report",
    { mirrorOutput: mirrorToolOutput }
  );
}

if (writeLcov) {
  runCargoLlvmCov(
    ["report", "--lcov", "--output-path", `${outputDir}/lcov.info`],
    "generate Rust LCOV coverage report",
    { mirrorOutput: mirrorToolOutput }
  );
}

if (writeHtml) {
  runCargoLlvmCov(
    ["report", "--html", "--output-dir", `${outputDir}/html`],
    "generate Rust HTML coverage report",
    { mirrorOutput: mirrorToolOutput }
  );
}

printCoverageSummary(absoluteSummaryReportPath, {
  attentionLineThreshold,
  fullReport
});
console.log(`Rust coverage report: ${absoluteTextReportPath}`);
console.log(`Rust coverage summary: ${absoluteSummaryReportPath}`);
console.log(`Rust coverage pretty summary: ${absolutePrettySummaryReportPath}`);
console.log(`Rust coverage full summary: ${absoluteFullSummaryReportPath}`);
console.log(`Rust coverage full pretty summary: ${absoluteFullPrettySummaryReportPath}`);

// Prints command-line options for local coverage runs.
function printUsage() {
  console.log("Usage: npm run coverage:rust -- [options] [-- cargo-llvm-cov args]");
  console.log("");
  console.log("Options:");
  console.log("  --full                 print cargo output and write unfiltered summary.json");
  console.log("  --threshold <percent>  default summary attention threshold; defaults to 90");
  console.log("  --no-clean             reuse previous cargo-llvm-cov state");
  console.log("  --show-missing-lines   include missing line details in coverage.txt");
  console.log("  --html | --json | --lcov");
}

// Returns true when the cargo-llvm-cov subcommand is available on PATH.
function hasCargoLlvmCov() {
  const result = spawnSync("cargo", ["llvm-cov", "--version"], {
    cwd: root,
    encoding: "utf8"
  });

  return result.status === 0;
}

// Prints setup guidance that works for this repo without mutating local tools.
function printSetupGuidance() {
  console.error("Rust coverage requires cargo-llvm-cov.");
  console.error("");
  console.error("Install one supported path, then rerun `npm run coverage:rust`:");
  console.error("");
  console.error("  rustup component add llvm-tools-preview");
  console.error("  cargo install cargo-llvm-cov --locked --version 0.6.15");
  console.error("");
  console.error("Version 0.6.15 supports the repo's current Rust 1.78 toolchain. With a");
  console.error("newer Rust toolchain, a current prebuilt cargo-llvm-cov binary is also fine.");
}

// Warns when rustup has not installed llvm-tools-preview for the active toolchain.
function warnWhenLlvmToolsPreviewIsMissing() {
  const result = spawnSync("rustup", ["component", "list", "--installed"], {
    cwd: root,
    encoding: "utf8"
  });

  if (result.status !== 0 || result.stdout.includes("llvm-tools")) {
    return;
  }

  console.warn("Warning: llvm-tools-preview is not installed for the active toolchain.");
  console.warn("cargo-llvm-cov may request it; install with `rustup component add llvm-tools-preview`.");
}

// Runs a cargo-llvm-cov command and returns the captured text.
function runCargoLlvmCov(cargoArgs, description, options = {}) {
  const mirrorOutput = options.mirrorOutput ?? true;
  console.log(`Running cargo llvm-cov to ${description}...`);
  const result = spawnSync("cargo", ["llvm-cov", ...cargoArgs], {
    cwd: root,
    encoding: "utf8",
    maxBuffer: 128 * 1024 * 1024
  });
  const output = `${result.stdout ?? ""}${result.stderr ?? ""}`;

  if (result.status !== 0) {
    if (output.length > 0) {
      process.stderr.write(output);
    }
    process.exitCode = result.status ?? 1;
    process.exit();
  }

  if (mirrorOutput && output.length > 0) {
    process.stdout.write(output);
  }

  return { output };
}

// Prints totals and source files that need coverage attention.
function printCoverageSummary(summaryPath, options) {
  const summary = JSON.parse(readFileSync(summaryPath, "utf8"));
  const workspace = summary.data?.[0];
  if (!workspace?.totals) {
    console.warn(`Coverage summary did not contain workspace totals: ${summaryPath}`);
    return;
  }

  const totals = workspace.totals;
  console.log("");
  console.log("Rust coverage totals:");
  console.log(`  lines:     ${formatCoverage(totals.lines)}`);
  console.log(`  functions: ${formatCoverage(totals.functions)}`);
  console.log(`  regions:   ${formatCoverage(totals.regions)}`);

  const files = [...(workspace.files ?? [])].map((file) => coverageFileRow(file));
  const filter = summary.ofgCoverageFilter;
  const reportFiles = options.fullReport
    ? files.sort((left, right) => right.uncoveredLines - left.uncoveredLines)
    : files;

  if (options.fullReport) {
    console.log("  all files:");
  } else {
    const ignoredCount = filter?.ignoredFileCount ?? 0;
    console.log(
      `  files below ${options.attentionLineThreshold}% line coverage ` +
        `(excluding ${ignoredCount} default-ignored file(s); use --full to include them):`
    );
  }

  if (reportFiles.length === 0) {
    console.log("    none");
    return;
  }

  for (const file of reportFiles) {
    console.log(
      `    ${file.relativePath}: lines ${formatCoverage(file.lines)}, ` +
        `functions ${formatCoverage(file.functions)}, regions ${formatCoverage(file.regions)}; ` +
        `${file.uncoveredLines} line(s) uncovered`
    );
  }
}

// Builds a normalized coverage row for console reporting.
function coverageFileRow(file) {
  const lines = file.summary?.lines;
  return {
    path: file.filename,
    relativePath: relativeCoveragePath(file.filename),
    lineCount: lines?.count ?? 0,
    linePercent: lines?.percent ?? 0,
    lines,
    functions: file.summary?.functions,
    regions: file.summary?.regions,
    uncoveredLines: uncoveredCount(lines)
  };
}

// Returns true for files that should not appear in the default attention list.
function isDefaultIgnoredCoverageFile(file) {
  const relativePath = file.relativePath.toLowerCase();
  const parts = relativePath.split("/");
  const fileName = parts.at(-1) ?? "";

  return (
    relativePath.startsWith("crates/ofg_test_harness/") ||
    parts.includes("tests") ||
    fileName === "facade.rs" ||
    fileName === "lib.rs" ||
    fileName === "tests.rs" ||
    fileName.endsWith("_tests.rs") ||
    fileName.endsWith(".test.rs") ||
    fileName.endsWith(".spec.rs")
  );
}

// Writes the default reduced summary, or the full cargo summary when --full is used.
function writeCoverageSummary(inputPath, outputPath, options) {
  const summary = JSON.parse(readFileSync(inputPath, "utf8"));
  const workspace = summary.data?.[0];
  if (!workspace?.files) {
    writeFileSync(outputPath, `${JSON.stringify(summary)}\n`);
    return;
  }

  const rows = workspace.files.map((file) => coverageFileRow(file));
  const ignoredFileCount = rows.filter((file) => isDefaultIgnoredCoverageFile(file)).length;
  const filteredFiles = options.fullReport
    ? workspace.files
    : workspace.files
        .filter((file) => !isDefaultIgnoredCoverageFile(coverageFileRow(file)))
        .filter((file) => {
          const lines = file.summary?.lines;
          return (lines?.count ?? 0) > 0 && (lines?.percent ?? 0) < options.attentionLineThreshold;
        })
        .sort((left, right) => {
          const leftLines = left.summary?.lines;
          const rightLines = right.summary?.lines;
          return (
            (leftLines?.percent ?? 0) - (rightLines?.percent ?? 0) ||
            uncoveredCount(rightLines) - uncoveredCount(leftLines)
          );
        });

  if (options.fullReport) {
    filteredFiles.sort(
      (left, right) => uncoveredCount(right.summary?.lines) - uncoveredCount(left.summary?.lines)
    );
  }

  const outputFiles = options.fullReport
    ? filteredFiles
    : filteredFiles.map((file) => ({
        ...file,
        filename: relativeCoveragePath(file.filename)
      }));
  const outputData = summary.data.map((entry, index) =>
    index === 0
      ? {
          ...entry,
          files: outputFiles
        }
      : entry
  );
  const output = {
    ofgCoverageFilter: {
      mode: options.fullReport ? "full" : "attention",
      lineThreshold: options.attentionLineThreshold,
      excludedByDefault: [
        "crates/ofg_test_harness/**",
        "**/tests/**",
        "**/facade.rs",
        "**/lib.rs",
        "**/tests.rs",
        "**/*_tests.rs",
        "**/*.test.rs",
        "**/*.spec.rs"
      ],
      sourceFileCount: rows.length,
      ignoredFileCount,
      reportedFileCount: filteredFiles.length,
      note:
        "Workspace totals remain cargo-llvm-cov totals; files is the filtered human-facing list."
    },
    ...summary,
    data: outputData
  };

  writeFileSync(outputPath, `${JSON.stringify(output)}\n`);
}

// Writes an indented copy of a compact JSON report for human inspection.
function writePrettyJson(inputPath, outputPath) {
  const json = JSON.parse(readFileSync(inputPath, "utf8"));
  writeFileSync(outputPath, `${JSON.stringify(json, null, 2)}\n`);
}

// Parses the default attention threshold used for console output.
function parseAttentionLineThreshold(scriptArgs) {
  let rawThreshold;

  for (let index = 0; index < scriptArgs.length; index += 1) {
    const arg = scriptArgs[index];
    if (arg.startsWith("--threshold=")) {
      rawThreshold = arg.slice("--threshold=".length);
    } else if (arg === "--threshold") {
      rawThreshold = scriptArgs[index + 1];
      index += 1;
    }
  }

  if (rawThreshold === undefined) {
    return defaultAttentionLineThreshold;
  }

  const threshold = Number(rawThreshold);
  if (!Number.isFinite(threshold) || threshold < 0 || threshold > 100) {
    console.error(`Invalid coverage threshold: ${rawThreshold}`);
    console.error("Use a number between 0 and 100, for example `--threshold 90`.");
    process.exitCode = 1;
    process.exit();
  }

  return threshold;
}

// Converts cargo-llvm-cov absolute filenames into repository-relative paths.
function relativeCoveragePath(path) {
  const normalizedRoot = root.replaceAll("\\", "/");
  const normalizedPath = path.replaceAll("\\", "/");
  if (normalizedPath.startsWith(`${normalizedRoot}/`)) {
    return normalizedPath.slice(normalizedRoot.length + 1);
  }
  return normalizedPath;
}

// Formats one cargo-llvm-cov summary bucket as covered/count and percent.
function formatCoverage(bucket) {
  const count = bucket?.count ?? 0;
  const covered = bucket?.covered ?? 0;
  const percent = bucket?.percent ?? 0;
  return `${covered}/${count} (${percent.toFixed(1)}%)`;
}

// Returns the uncovered item count for one cargo-llvm-cov summary bucket.
function uncoveredCount(bucket) {
  const count = bucket?.count ?? 0;
  const covered = bucket?.covered ?? 0;
  return Math.max(0, count - covered);
}
