#!/usr/bin/env node
/**
 * Fails when the hand-written script typings have drifted from the C++ they describe.
 *
 * The typings exist so scripts can be type-checked on a machine with no editor and no script
 * VM installed -- a fresh checkout, a review, CI. That convenience comes with a specific and
 * nasty failure mode: a stale declaration type-checks a call that does not exist at runtime,
 * so the compiler says the code is fine and the game says nothing at all, because an unbound
 * rule just falls back to its C++ default. A stub nobody checks is worse than no stub.
 *
 * So this reads the UFUNCTION / UPROPERTY / USTRUCT declarations out of the script-facing
 * headers and compares them, by name and by argument count, against the .d.ts files. Anything
 * in C++ and missing from the typings is an error; anything in the typings with no C++ behind
 * it is an error too, and that direction is the one that matters -- it is the invented call.
 *
 * What it deliberately does not check: types. Mapping FString to string and TSoftObjectPtr to
 * whatever the VM decides is the binding generator's job, and re-implementing its opinions
 * here would produce a checker that is wrong in its own way. Name and arity catch the drift
 * that actually happens: a parameter added, a function renamed, a helper deleted.
 *
 * Usage
 *     node Plugins/ScriptCore/TypeScript/tools/check-api-parity.mjs
 *     node Plugins/ScriptCore/TypeScript/tools/check-api-parity.mjs --list
 */

import { readFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const SUCCESS_MARKER = "SCRIPT_API_PARITY_OK";

// <repo>/Plugins/ScriptCore/TypeScript/tools -> <repo>
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..", "..");

/**
 * Each pair is "these headers must be described by these typings".
 *
 * Adding a scripted GameFeature adds a pair here. That is on purpose: a feature whose typings
 * nobody checks is exactly the situation this file exists to prevent, and a glob would let one
 * appear without anybody noticing.
 */
const PAIRS = [
	{
		name: "ScriptCore",
		headers: ["Plugins/ScriptCore/Source/ScriptCoreRuntime/Public/Script"],
		typings: ["Plugins/ScriptCore/TypeScript/typings/ue-core.d.ts"],
	},
	{
		name: "OceanAdventure",
		headers: ["Plugins/GameFeatures/OceanAdventure/Source/OceanAdventureRuntime/Public/Script"],
		typings: ["Plugins/GameFeatures/OceanAdventure/TypeScript/typings/ue-oceanadventure.d.ts"],
	},
];

/** UHT strips these prefixes; the binding generator exposes the bare name. */
function scriptName(cppName) {
	return /^[UAF][A-Z]/.test(cppName) ? cppName.slice(1) : cppName;
}

/**
 * Counts parameters by splitting on top-level commas, so TArray<A, B> stays one parameter and
 * a trailing comma -- which the TypeScript side is formatted with -- does not invent another.
 */
function countParameters(parameterText) {
	const trimmed = parameterText.trim();
	if (trimmed === "" || trimmed === "void") {
		return 0;
	}

	const segments = [];
	let depth = 0;
	let current = "";

	for (const character of trimmed) {
		if ("<([{".includes(character)) {
			depth += 1;
		} else if (">)]}".includes(character)) {
			depth -= 1;
		}

		if (character === "," && depth === 0) {
			segments.push(current);
			current = "";
		} else {
			current += character;
		}
	}

	segments.push(current);

	return segments.filter((segment) => segment.trim() !== "").length;
}

/** Everything one header publishes to scripts. */
function parseHeader(source) {
	const types = new Map();

	// Comments would otherwise contribute stray braces and parentheses to the scanner.
	const text = source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");

	const typePattern = /\b(UCLASS|USTRUCT)\s*\([^)]*\)\s*(?:class|struct)\s+(?:[A-Z_]+_API\s+)?([A-Za-z_]\w*)/g;

	let typeMatch;
	while ((typeMatch = typePattern.exec(text)) !== null) {
		const name = scriptName(typeMatch[2]);
		const body = extractBody(text, typePattern.lastIndex);
		types.set(name, parseMembers(body));
	}

	return types;
}

/** The braced block that follows a class or struct header, brace-counted. */
function extractBody(text, fromIndex) {
	const open = text.indexOf("{", fromIndex);
	if (open < 0) {
		return "";
	}

	let depth = 0;

	for (let index = open; index < text.length; index += 1) {
		const character = text[index];
		if (character === "{") {
			depth += 1;
		} else if (character === "}") {
			depth -= 1;
			if (depth === 0) {
				return text.slice(open + 1, index);
			}
		}
	}

	return text.slice(open + 1);
}

function parseMembers(body) {
	const members = new Map();

	// A UFUNCTION's own parentheses may nest (meta = (WorldContext = "x")), so the macro
	// arguments are skipped by counting rather than by a lazy match that stops at the first
	// close paren and leaves the scanner inside the specifier list.
	const macroPattern = /\b(UFUNCTION|UPROPERTY)\s*\(/g;

	let macroMatch;
	while ((macroMatch = macroPattern.exec(body)) !== null) {
		const macroOpen = macroMatch.index + macroMatch[0].length - 1;
		const afterMacro = skipParens(body, macroOpen);
		if (afterMacro < 0) {
			continue;
		}

		// Only Blueprint-exposed members count as the boundary.
		//
		// A script VM reaches anything reflected, private members included, so this is a
		// narrower rule than "what is technically callable" -- deliberately. The typings are
		// the written-down contract of what scripts are *meant* to use; requiring a private
		// UPROPERTY to appear in them would push internals into the contract and make every
		// refactor of a private field a change to the script API.
		const specifiers = body.slice(macroOpen, afterMacro);
		if (!/Blueprint/.test(specifiers)) {
			macroPattern.lastIndex = afterMacro;
			continue;
		}

		const declaration = body.slice(afterMacro, findDeclarationEnd(body, afterMacro));

		if (macroMatch[1] === "UFUNCTION") {
			// An inline body and the terminating semicolon both have to go before the
			// signature is matched: the pattern anchors at the end, and a stray ";" left on
			// the tail silently matched nothing -- which looked exactly like a header with no
			// UFUNCTIONs in it, i.e. like a passing check.
			const signatureText = declaration
				.replace(/\s*\{[\s\S]*$/, "")
				.replace(/;\s*$/, "")
				.trim();

			const signature = /([A-Za-z_]\w*)\s*\(([\s\S]*)\)\s*(?:const)?\s*$/.exec(signatureText);

			if (signature) {
				members.set(signature[1], { kind: "function", parameters: countParameters(signature[2]) });
			}
		} else {
			const property = /([A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$/.exec(declaration.trim());
			if (property) {
				members.set(property[1], { kind: "property" });
			}
		}

		macroPattern.lastIndex = afterMacro;
	}

	return members;
}

/** Index just past the parenthesis group that starts at openIndex. */
function skipParens(text, openIndex) {
	let depth = 0;

	for (let index = openIndex; index < text.length; index += 1) {
		if (text[index] === "(") {
			depth += 1;
		} else if (text[index] === ")") {
			depth -= 1;
			if (depth === 0) {
				return index + 1;
			}
		}
	}

	return -1;
}

/** End of the declaration after a macro: its semicolon, or the body of an inline function. */
function findDeclarationEnd(text, fromIndex) {
	const semicolon = text.indexOf(";", fromIndex);
	const brace = text.indexOf("{", fromIndex);

	if (semicolon < 0) {
		return brace < 0 ? text.length : brace;
	}
	if (brace < 0 || semicolon < brace) {
		return semicolon + 1;
	}

	return brace;
}

/** Everything one .d.ts declares inside `declare namespace UE`. */
function parseTypings(source) {
	const types = new Map();
	const text = source
		.replace(/\/\*[\s\S]*?\*\//g, "")
		.replace(/\/\/[^\n]*/g, "")
		// An arrow's ">" closes a generic that was never opened, which sent the depth scan
		// negative from the first delegate-typed property onwards and made every member after
		// it invisible. Replacing it with a two-character stand-in keeps every offset intact.
		.replace(/=>/g, "==");

	const typePattern = /\bclass\s+([A-Za-z_]\w*)(?:\s+extends\s+[A-Za-z_.]\w*)?\s*\{/g;

	let typeMatch;
	while ((typeMatch = typePattern.exec(text)) !== null) {
		const body = extractBody(text, typeMatch.index);
		types.set(typeMatch[1], parseTypingMembers(body));
	}

	return types;
}

/**
 * Members declared directly on a type, at brace depth zero.
 *
 * The depth test is the whole trick. A multi-line parameter list looks exactly like a run of
 * property declarations -- `Target: Actor,` on its own line is both -- so a naive line scan
 * reports every parameter of every function as a member of the class, and then reports each
 * of them as invented. The first version of this file did precisely that.
 */
function parseTypingMembers(body) {
	const members = new Map();
	const depths = computeDepths(body);
	const memberPattern = /(?:^|\n)([ \t]*)(static\s+)?([A-Za-z_]\w*)\s*(\(|:)/g;

	let memberMatch;
	while ((memberMatch = memberPattern.exec(body)) !== null) {
		const nameIndex = memberMatch.index + memberMatch[0].lastIndexOf(memberMatch[3]);

		if (depths[nameIndex] !== 0) {
			continue;
		}

		const name = memberMatch[3];

		if (memberMatch[4] === "(") {
			const open = body.indexOf("(", nameIndex);
			const close = skipParens(body, open);
			members.set(name, {
				kind: "function",
				parameters: countParameters(body.slice(open + 1, close - 1)),
			});

			// Past the parameter list, so nothing inside it is read as another member.
			memberPattern.lastIndex = close;
		} else {
			members.set(name, { kind: "property" });
		}
	}

	return members;
}

/** Bracket depth at every offset, so a scan can ask whether it is inside a parameter list. */
function computeDepths(text) {
	const depths = new Array(text.length).fill(0);
	let depth = 0;

	for (let index = 0; index < text.length; index += 1) {
		const character = text[index];

		if ("<([{".includes(character)) {
			depths[index] = depth;
			depth += 1;
			continue;
		}

		if (">)]}".includes(character)) {
			depth -= 1;
		}

		depths[index] = depth;
	}

	return depths;
}

async function readAllHeaders(directories) {
	const { readdir } = await import("node:fs/promises");
	const merged = new Map();

	for (const directory of directories) {
		const absolute = path.join(repoRoot, directory);
		const entries = await readdir(absolute, { withFileTypes: true, recursive: true });

		for (const entry of entries) {
			if (!entry.isFile() || !entry.name.endsWith(".h")) {
				continue;
			}

			const parent = entry.parentPath ?? entry.path ?? absolute;
			const source = await readFile(path.join(parent, entry.name), "utf8");

			for (const [name, members] of parseHeader(source)) {
				merged.set(name, members);
			}
		}
	}

	return merged;
}

async function readAllTypings(files) {
	const merged = new Map();

	for (const file of files) {
		const source = await readFile(path.join(repoRoot, file), "utf8");
		for (const [name, members] of parseTypings(source)) {
			merged.set(name, members);
		}
	}

	return merged;
}

/** Declared in the typings but not reflected: engine types the stub only sketches. */
const NOT_OURS = new Set([
	"Object",
	"Actor",
	"World",
	"Vector",
	"Rotator",
	"GameplayTag",
	"TArray",
]);

async function main() {
	const listOnly = process.argv.includes("--list");
	const problems = [];

	for (const pair of PAIRS) {
		const cppTypes = await readAllHeaders(pair.headers);
		const tsTypes = await readAllTypings(pair.typings);

		if (listOnly) {
			console.log(`${pair.name}: ${cppTypes.size} reflected type(s), ${tsTypes.size} declared`);
			for (const [name, members] of cppTypes) {
				console.log(`  ${name} (${members.size} member(s))${tsTypes.has(name) ? "" : "  [not declared]"}`);
			}
			continue;
		}

		for (const [typeName, cppMembers] of cppTypes) {
			const tsMembers = tsTypes.get(typeName);

			if (!tsMembers) {
				if (cppMembers.size > 0) {
					problems.push(`${pair.name}: ${typeName} is reflected in C++ but declared nowhere in the typings`);
				}
				continue;
			}

			for (const [memberName, cppMember] of cppMembers) {
				const tsMember = tsMembers.get(memberName);

				if (!tsMember) {
					problems.push(`${pair.name}: ${typeName}.${memberName} exists in C++ but not in the typings`);
					continue;
				}

				if (cppMember.kind !== tsMember.kind) {
					problems.push(
						`${pair.name}: ${typeName}.${memberName} is a ${cppMember.kind} in C++ and a ${tsMember.kind} in the typings`,
					);
					continue;
				}

				if (cppMember.kind === "function" && cppMember.parameters !== tsMember.parameters) {
					problems.push(
						`${pair.name}: ${typeName}.${memberName} takes ${cppMember.parameters} argument(s) in C++ ` +
							`and ${tsMember.parameters} in the typings`,
					);
				}
			}
		}

		for (const [typeName, tsMembers] of tsTypes) {
			if (NOT_OURS.has(typeName)) {
				continue;
			}

			const cppMembers = cppTypes.get(typeName);
			if (!cppMembers) {
				problems.push(`${pair.name}: ${typeName} is declared in the typings but is not a reflected C++ type`);
				continue;
			}

			for (const memberName of tsMembers.keys()) {
				if (!cppMembers.has(memberName)) {
					problems.push(
						`${pair.name}: ${typeName}.${memberName} is declared in the typings with no UFUNCTION or UPROPERTY behind it`,
					);
				}
			}
		}
	}

	if (listOnly) {
		return;
	}

	if (problems.length > 0) {
		console.error("Script API parity check failed:\n");
		for (const problem of problems) {
			console.error(`  - ${problem}`);
		}
		console.error("\nFix the typings, or the C++, so that they describe the same boundary.");
		process.exit(1);
	}

	console.log(SUCCESS_MARKER);
}

await main();
