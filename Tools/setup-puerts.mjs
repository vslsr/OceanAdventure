#!/usr/bin/env node
/**
 * Reports whether PuerTS is installed for this checkout, and prints how to install it.
 *
 * PuerTS is not committed, for the same reason the EOS SDK is not: it carries prebuilt V8
 * binaries, hundreds of megabytes of them, per platform. So it is a per-machine install and
 * this script is the thing that says so out loud -- because the failure mode without it is
 * bad. The project builds (ScriptCoreRuntime compiles against its null backend), the editor
 * opens, the game plays, and every scripted rule quietly falls back to its C++ default. A
 * balance pass would edit TypeScript for an hour and see no change at all.
 *
 * It deliberately does not download anything. Fetching a hundreds-of-megabytes plugin on
 * somebody's behalf, into a path guessed from their machine, is exactly the kind of help that
 * ends in a half-extracted plugin and a build error that names neither.
 *
 * Usage
 *     node Tools/setup-puerts.mjs
 */

import { existsSync, readdirSync, statSync } from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const SUCCESS_MARKER = "PUERTS_PRESENT";
const MISSING_MARKER = "PUERTS_MISSING";

// <repo>/Tools -> <repo>. Derived, never spelled out: this repository bans absolute paths
// because a path from one machine is what stops a checkout working on the next one.
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const pluginsDir = path.join(repoRoot, "Plugins");

/**
 * Finds the JsEnv module, which is what the build actually probes for.
 *
 * Looking for the module rather than for a folder called "Puerts": releases disagree on the
 * folder's capitalisation and some checkouts nest it a level deeper, and a probe that missed
 * for either reason would send somebody re-installing a plugin they already had.
 */
function findJsEnv(directory, depth = 0) {
	if (depth > 4 || !existsSync(directory)) {
		return null;
	}

	for (const entry of readdirSync(directory, { withFileTypes: true })) {
		if (!entry.isDirectory()) {
			continue;
		}

		const child = path.join(directory, entry.name);

		if (entry.name === "JsEnv" && existsSync(path.join(child, "JsEnv.Build.cs"))) {
			return child;
		}

		const found = findJsEnv(child, depth + 1);
		if (found) {
			return found;
		}
	}

	return null;
}

const jsEnv = findJsEnv(pluginsDir);

if (jsEnv) {
	console.log(`${SUCCESS_MARKER} ${path.relative(repoRoot, jsEnv)}`);

	const binaries = path.join(path.dirname(path.dirname(jsEnv)), "Binaries");
	if (!existsSync(binaries) || !statSync(binaries).isDirectory()) {
		console.log(
			"Note: the plugin has no Binaries/ yet. That is expected before the first build; " +
				"if it is still missing after one, the V8 libraries were not unpacked with the plugin.",
		);
	}

	process.exit(0);
}

console.log(MISSING_MARKER);
console.log("");
console.log("PuerTS is not installed in this checkout, so no TypeScript will run.");
console.log("The project still builds and plays: every scripted rule falls back to its C++");
console.log("default, silently. That is the state you are in right now.");
console.log("");
console.log("To install it:");
console.log("");
console.log("  1. Get a PuerTS release that supports this project's engine version (UE 5.7),");
console.log("     including its prebuilt V8 binaries -- the source alone will not link.");
console.log("  2. Unpack it so that the module lands at:");
console.log("");
console.log("         Plugins/Puerts/Source/JsEnv/JsEnv.Build.cs");
console.log("");
console.log("     Any folder name works; the build looks for JsEnv.Build.cs, not for a name.");
console.log("  3. Re-run this script. It should print PUERTS_PRESENT.");
console.log("  4. Rebuild the editor. ScriptCoreRuntime picks the plugin up at build time,");
console.log("     not at runtime, so a build from before the install still has no VM.");
console.log("  5. In the editor, run `script.status` in the console. It prints backend=PuerTS");
console.log("     when the VM is live, and backend=Null when the rebuild has not happened.");
console.log("");
console.log("Plugins/Puerts/ is git-ignored, so it stays out of this repository.");

process.exit(1);
