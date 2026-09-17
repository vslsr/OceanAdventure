/**
 * One definition of how a script bundle is built, shared by the builder and the staleness
 * check.
 *
 * Shared rather than duplicated because the staleness check works by rebuilding and comparing:
 * if it built with even slightly different options, it would report a mismatch on a bundle
 * that was perfectly up to date, and the fix everyone would reach for is to stop running it.
 */

import { existsSync } from "node:fs";
import { readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

// <repo>/Plugins/ScriptCore/TypeScript/tools/lib -> <repo>
export const repoRoot = path.resolve(
	path.dirname(fileURLToPath(import.meta.url)),
	"..",
	"..",
	"..",
	"..",
	"..",
);

/** <feature>/TypeScript -> <feature>/Content/Script/main.js */
export function outputFor(projectDir) {
	return path.join(projectDir, "..", "Content", "Script", "main.js");
}

export function entryFor(projectDir) {
	return path.join(projectDir, "src", "main.ts");
}

export function buildOptions(projectDir, overrides = {}) {
	return {
		// Pinned so the bundle does not depend on where the command was run from. esbuild
		// writes each module's path into a comment relative to its working directory, so
		// without this the same sources produce different bytes from the project folder and
		// from the repository root -- and the staleness check, which runs from the root,
		// would call a freshly built bundle stale.
		absWorkingDir: path.resolve(projectDir),
		entryPoints: [entryFor(projectDir)],
		outfile: outputFor(projectDir),
		bundle: true,
		format: "iife",
		// "neutral" because the target is neither a browser nor Node: no DOM, no require, no
		// process. It also means esbuild ignores package "main" fields unless asked, which is
		// how the framework package is resolved.
		platform: "neutral",
		mainFields: ["main"],
		target: "es2020",
		// Readable rather than small. These bundles are a few kilobytes, they are read once
		// per map load, and what actually costs time is reading a stack trace from a playtest
		// log that points into a minified line.
		minify: false,
		sourcemap: false,
		legalComments: "none",
		banner: {
			js:
				"// Generated from ../TypeScript/src by tools/build-bundle.mjs. Do not edit.\n" +
				"// Rebuild with `npm run build`; `npm run check:dist` fails when this is stale.\n",
		},
		...overrides,
	};
}

/**
 * Every plugin or GameFeature that ships scripts, found by looking for the entry point.
 *
 * Discovered rather than listed: a feature's scripts live inside the feature, so adding one
 * should not mean editing a manifest somewhere else that the author has no reason to know
 * about. The API parity check takes the opposite line for its own list, and on purpose --
 * missing a bundle here costs a rebuild, missing a typings pair there costs a silent gap in
 * the only thing that keeps the typings honest.
 */
export async function discoverProjects() {
	const found = [];
	const roots = [path.join(repoRoot, "Plugins"), path.join(repoRoot, "Plugins", "GameFeatures")];

	for (const root of roots) {
		if (!existsSync(root)) {
			continue;
		}

		for (const entry of await readdir(root, { withFileTypes: true })) {
			if (!entry.isDirectory()) {
				continue;
			}

			const projectDir = path.join(root, entry.name, "TypeScript");
			if (existsSync(entryFor(projectDir))) {
				found.push(projectDir);
			}
		}
	}

	return found.sort();
}
