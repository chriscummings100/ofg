// Deploys the packaged OFG browser app to Cloudflare Pages.
//
// The script uses the local Wrangler dependency from node_modules/.bin. It does
// not call npx, does not install Wrangler, and does not install compiler
// toolchains. Local Wrangler authentication is expected for interactive deploys.
import { existsSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const deployDir = resolve(root, ".deploy");
const args = parseArgs(process.argv.slice(2));
const projectName = args.projectName ?? process.env.CLOUDFLARE_PAGES_PROJECT_NAME;

if (!projectName) {
  throw new Error("Missing Cloudflare Pages project name. Use --project-name=ofg or CLOUDFLARE_PAGES_PROJECT_NAME.");
}

runNpmScript("package:site");
verifyDeployPackage();

const wrangler = localWranglerCommand();
const wranglerArgs = ["pages", "deploy", ".deploy", "--project-name", projectName];
if (args.dryRun) {
  console.log(`Dry run: ${wrangler.command} ${[...wrangler.args, ...wranglerArgs].join(" ")}`);
} else {
  run(wrangler.command, [...wrangler.args, ...wranglerArgs]);
}

// Parses the small deploy argument surface.
function parseArgs(argv) {
  const parsed = { dryRun: false, projectName: undefined };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--dry-run") {
      parsed.dryRun = true;
    } else if (arg === "--project-name") {
      parsed.projectName = argv[index + 1];
      index += 1;
    } else if (arg.startsWith("--project-name=")) {
      parsed.projectName = arg.slice("--project-name=".length);
    } else {
      throw new Error(`Unknown deploy argument: ${arg}`);
    }
  }
  return parsed;
}

// Returns the local Wrangler package entrypoint without spawning a .cmd shim.
function localWranglerCommand() {
  const entrypoint = resolve(root, "node_modules", "wrangler", "bin", "wrangler.js");
  if (!existsSync(entrypoint)) {
    throw new Error("Local Wrangler is missing. Run npm install before deploying.");
  }
  return { command: process.execPath, args: [entrypoint] };
}

// Verifies the package exists before handing it to Wrangler.
function verifyDeployPackage() {
  const requiredFiles = [
    "_headers",
    "index.html",
    "dist/app/main.js",
    "dist/app/canvasHost.js",
    "dist/app/wasmRuntime.js",
    "src/app/styles.css",
    "assets/wasm/ofg_cpp/ofg_cpp.js",
    "assets/wasm/ofg_cpp/ofg_cpp.wasm"
  ];
  for (const relativePath of requiredFiles) {
    const fullPath = resolve(deployDir, relativePath);
    if (!existsSync(fullPath) || !statSync(fullPath).isFile()) {
      throw new Error(`Deploy package is missing required file: ${relativePath}`);
    }
  }
}

// Runs an npm script in a platform-compatible way.
function runNpmScript(scriptName) {
  const npmExecPath = process.env.npm_execpath;
  if (npmExecPath && existsSync(npmExecPath)) {
    run(process.execPath, [npmExecPath, "run", scriptName]);
    return;
  }

  const npmCommand = process.platform === "win32" ? "npm.cmd" : "npm";
  run(npmCommand, ["run", scriptName], { shell: process.platform === "win32" });
}

// Runs a child command and exits with the same failing status.
function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: process.env,
    stdio: "inherit",
    ...options
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
