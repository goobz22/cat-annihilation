// spell_spam_probe — pins the web-parity "spells are spammable" contract.
//
// The web has NO spell cooldown, mana pool, or cast XP: LocalProjectileSystem
// spawns a bolt unconditionally on every input press and magic XP is awarded
// only on hit/kill. Native's castSpell used to gate every cast behind a 1.0 s
// cooldown + mana + a 5-XP cast grant, so rapid-firing the water spell landed
// 1 cast where the web lands 3 (the PARITY_MATRIX "native spell cooldown (web
// spammable)" recorded delta, resolved 2026-07-18 by parity-gating all three
// native resource loops off under WebParity::kEnabled).
//
// The probe drives the real game headlessly (key:1 + three space presses in
// ~0.6 s) and counts the "[attack] water_bolt cast OK" log lines: parity
// requires ALL THREE casts to land. Revert-refail: with the cooldown gate
// restored the count is exactly 1 and this exits 1.
//
//   bun scripts/spell_spam_probe.ts
//
// Exit 0 = 3/3 casts landed; 1 = casts were throttled; 2 = infra error.

import { readFileSync, existsSync } from "fs";
import { spawnSync } from "child_process";
import { join, dirname } from "path";

const repoRoot = dirname(import.meta.dir);
const outDir = join(repoRoot, "build-ninja", "headless", "spell-spam");

const SCRIPT = [
    "wait:3", "expect:state=MainMenu",
    "click:0.39,0.48", "wait:1.2", "click:0.654,0.664", "wait:2",
    "expect:state=Playing",
    "key:1", "key:space", "wait:0.25", "key:space", "wait:0.25", "key:space",
    "wait:0.5", "quit",
].join(";");

const run = spawnSync("bun", [
    join(repoRoot, "scripts", "headless_run.ts"),
    "--script", SCRIPT, "--out", outDir, "--timeout", "60",
], { encoding: "utf8", windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
if (run.status !== 0) {
    console.error("spell_spam_probe: drive run FAILED");
    console.error((run.stdout ?? "").split("\n").slice(-6).join("\n"));
    process.exit(2);
}

const logPath = join(outDir, "run.log");
if (!existsSync(logPath)) {
    console.error("spell_spam_probe: run.log missing — no evidence (absence is not a pass)");
    process.exit(2);
}
const casts = (readFileSync(logPath, "utf8").match(/water_bolt cast OK/g) ?? []).length;
if (casts >= 3) {
    console.log(`spell_spam_probe: OK — ${casts}/3 rapid casts landed (web-parity spammable)`);
    process.exit(0);
}
console.error(`spell_spam_probe: FAIL — only ${casts}/3 rapid casts landed; a native cooldown/mana gate is throttling spells the web fires freely`);
process.exit(1);
