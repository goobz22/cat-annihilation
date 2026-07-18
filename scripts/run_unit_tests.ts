// run_unit_tests — launch the Catch2 unit_tests.exe with its console HIDDEN.
//
// Why (2026-07-18 operator directive, second instance of the class): invoking
// build-ninja/tests/unit_tests.exe directly from a shell allocates a visible
// console window on the operator's desktop ("i just saw unit tests.exe appear
// on my screen"). Same class as the harness cmd-flashes fixed earlier: on
// Windows a console-subsystem child gets a visible console unless spawned with
// windowsHide: true. ALL automated unit-test runs must go through this wrapper
// (never the bare exe), exactly like all game runs go through headless_run.ts.
// lint-windows-hide covers this file's spawn automatically.
//
// Usage:
//   bun scripts/run_unit_tests.ts                       # full suite
//   bun scripts/run_unit_tests.ts "*some test filter*"  # Catch2 filter args pass through
//
// Exit code = the test exe's exit code; stdout/stderr are forwarded verbatim.

import { spawnSync } from "child_process";
import { existsSync } from "fs";
import { dirname, join } from "path";

const repoRoot = dirname(import.meta.dir);
const exePath = join(repoRoot, "build-ninja", "tests", "unit_tests.exe");

if (!existsSync(exePath)) {
    console.error(`run_unit_tests: ${exePath} not built — run: ninja -C build-ninja unit_tests`);
    process.exit(2);
}

const result = spawnSync(exePath, process.argv.slice(2), {
    cwd: join(repoRoot, "build-ninja", "tests"),
    encoding: "utf8",
    // The suite prints millions of assertion counts; keep headroom.
    maxBuffer: 64 * 1024 * 1024,
    windowsHide: true,
});

process.stdout.write(result.stdout ?? "");
process.stderr.write(result.stderr ?? "");
if (result.error) {
    console.error(`run_unit_tests: spawn error: ${result.error.message}`);
    process.exit(2);
}
process.exit(result.status ?? 1);
