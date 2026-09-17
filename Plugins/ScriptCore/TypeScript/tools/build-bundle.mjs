#!/usr/bin/env node
/**
 * Bundles a TypeScript project into the single file its plugin ships.
 *
 * One self-contained IIFE per feature, written to that feature's own Content/Script/main.js.
 * Not a module graph the engine resolves at runtime, and not one bundle for the whole project,
 * for two reasons that both matter:
 *
 *  - A resolver would have to be taught where each plugin's scripts live, and would then let
 *    one GameFeature import another's TypeScript. This repository forbids that dependency in
 *    C++ and in assets; letting it in through a `require` would be the same mistake with new
 *    syntax. Separate bundles make it impossible rather than discouraged.
 *  - The host reads the bundles itself and concatenates them, so booting needs no module
 *    loader -- which is the part of a script VM's API that varies most between releases, and
 *    the part most worth keeping out of the blast radius.
 *
 * The framework is bundled into each feature rather than shared. It is small, and a shared
 * copy would be exactly the cross-feature coupling the separation above exists to prevent.
 *
 * Usage
 *     node tools/build-bundle.mjs --project <dir>     # one project
 *     node tools/build-bundle.mjs                     # every project that ships scripts
 *     node tools/build-bundle.mjs --project <dir> --watch
 */

import { mkdir } from "node:fs/promises";
import path from "node:path";
import process from "node:process";

import * as esbuild from "esbuild";

import { buildOptions, discoverProjects, entryFor, outputFor } from "./lib/bundle.mjs";

const args = process.argv.slice(2);
const watch = args.includes("--watch");
const projectArgIndex = args.indexOf("--project");

const projects =
	projectArgIndex >= 0 ? [path.resolve(args[projectArgIndex + 1] ?? ".")] : await discoverProjects();

if (projects.length === 0) {
	console.error("No TypeScript project with a src/main.ts was found.");
	process.exit(1);
}

if (watch && projects.length !== 1) {
	console.error("--watch builds one project; pass --project <dir>.");
	process.exit(1);
}

for (const projectDir of projects) {
	const options = buildOptions(projectDir);
	await mkdir(path.dirname(outputFor(projectDir)), { recursive: true });

	if (watch) {
		const context = await esbuild.context(options);
		await context.watch();
		console.log(`watching ${rel(entryFor(projectDir))} -> ${rel(outputFor(projectDir))}`);
		console.log("The editor reloads the script VM by itself when that file changes.");
	} else {
		await esbuild.build(options);
		console.log(`built ${rel(outputFor(projectDir))}`);
	}
}

function rel(target) {
	return path.relative(process.cwd(), target);
}
