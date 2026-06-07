// Verifies repository command wiring that protects the Rust-first test split.
import { deepEqual, equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";

describe("package scripts", () => {
  it("keeps Rust, TypeScript, smoke, and coverage lanes explicit", () => {
    const packageJson = readJsonFile<PackageJson>("package.json");
    const scripts = packageJson.scripts;

    equal(scripts["test:rust"], "cargo test --workspace");
    ok(requiredScript(scripts, "test:ts").includes("tsc -p tsconfig.test.json"));
    ok(requiredScript(scripts, "test:ts").includes("mocha \"dist-test/**/*.test.js\""));
    equal(scripts.test, "npm run test:rust && npm run test:ts");
    equal(scripts["coverage:rust"], "node tools/rust-coverage.mjs");
    equal(scripts["smoke"], "npm run smoke:rust && npm run smoke:browser");
    ok(requiredScript(scripts, "smoke:rust").includes("cargo run -p ofg_test_harness"));
    ok(requiredScript(scripts, "smoke:browser").includes("node tools/browser-smoke.mjs"));
  });

  it("keeps terrain benchmarks in Rust instead of TypeScript terrain WASM", () => {
    const packageJson = readJsonFile<PackageJson>("package.json");
    const scripts = packageJson.scripts;

    equal(scripts["bench:terrain:wasm"], undefined);
    ok(requiredScript(scripts, "bench:terrain:rust").includes("cargo run -p ofg_test_harness"));
    ok(requiredScript(scripts, "bench:terrain:rust").includes("--bin ofg-terrain-bench"));
  });
});

describe("TypeScript test config", () => {
  it("compiles source contracts and repository tests into a separate output", () => {
    const tsconfig = readJsonFile<TsConfig>("tsconfig.test.json");

    equal(tsconfig.extends, "./tsconfig.app.json");
    equal(tsconfig.compilerOptions.rootDir, ".");
    equal(tsconfig.compilerOptions.outDir, "dist-test");
    deepEqual(tsconfig.include, ["src/**/*.ts", "src/**/*.d.ts", "tests/**/*.ts"]);
    deepEqual(tsconfig.exclude, []);
  });
});

type PackageJson = {
  readonly scripts: Record<string, string | undefined>;
};

type TsConfig = {
  readonly extends: string;
  readonly compilerOptions: {
    readonly rootDir: string;
    readonly outDir: string;
  };
  readonly include: string[];
  readonly exclude: string[];
};

function readJsonFile<T>(path: string): T {
  return JSON.parse(readFileSync(path, "utf8")) as T;
}

function requiredScript(scripts: Record<string, string | undefined>, name: string): string {
  const script = scripts[name];
  if (script === undefined) {
    throw new Error(`Missing package script '${name}'.`);
  }

  return script;
}
