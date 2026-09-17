#!/usr/bin/env node
/**
 * Fails when a committed script bundle no longer matches the TypeScript it was built from.
 *
 * The compiled bundles are committed, which is unusual enough to explain. The engine has no
 * Node, so a checkout that only has sources has no scripts at all -- which does not look
 * broken, because every scripted rule falls back to its C++ default and the game plays. A
 * cook would ship that silence. Committing the output keeps a clone runnable and a package
 * correct; the cost is that the output can drift from its source, and this check is what
 * turns that drift from invisible into a failing build.
 *
 * Usage
 *     node Plugins/ScriptCore/TypeScript/tools/check-dist-fresh.mjs
 */

import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";
import process from "node:process";

import * as esbuild from "esbuild";

import { buildOptions, discoverProjects, outputFor, repoRoot } from "./lib/bundle.mjs";

const SUCCESS_MARKER = "SCRIPT_DIST_FRESH_OK";

const stale = [];

for (const projectDir of await discoverProjects()) {
	const outFile = outputFor(projectDir);
	const relative = path.relative(repoRoot, outFile);

	const result = await esbuild.build(buildOptions(projectDir, { write: false }));
	const rebuilt = result.outputFiles[0]?.text ?? "";

	if (!existsSync(outFile)) {
		stale.push(`${relative} has never been built`);
		continue;
	}

	const committed = await readFile(outFile, "utf8");

	// Newlines only. A checkout on Windows can rewrite line endings on its way to disk, and a
	// check that failed for that alone would be noise nobody could act on.
	if (normalise(committed) !== normalise(rebuilt)) {
		stale.push(`${relative} is stale`);
	}
}

if (stale.length > 0) {
	console.error("Committed script bundles are out of date:\n");
	for (const entry of stale) {
		console.error(`  - ${entry}`);
	}
	console.error("\nRun `npm run build` and commit the result.");
	process.exit(1);
}

console.log(SUCCESS_MARKER);

function normalise(text) {
	return text.replace(/\r\n/g, "\n");
}
