/**
 * Logging for scripts.
 *
 * Everything goes through console, which the VM routes into the engine log, and everything
 * carries a feature prefix. That is not decoration: script output and engine output land in
 * the same file, and a line that does not say which bundle wrote it is unattributable in a
 * 200MB log from a playtest nobody was watching.
 */

export type LogLevel = "log" | "warn" | "error";

function write(level: LogLevel, feature: string, message: string, detail?: unknown): void {
	const line = `[ts:${feature}] ${message}`;

	// The VM's console is the only logging primitive the framework may assume. Anything
	// richer belongs to a gameplay layer, which is allowed to know about Unreal; this one
	// is not, for the same reason ScriptCore may not depend on LyraGame.
	if (detail === undefined) {
		console[level](line);
	} else {
		console[level](line, detail);
	}
}

export interface Logger {
	log(message: string, detail?: unknown): void;
	warn(message: string, detail?: unknown): void;
	error(message: string, detail?: unknown): void;
}

export function makeLogger(feature: string): Logger {
	return {
		log: (message, detail) => write("log", feature, message, detail),
		warn: (message, detail) => write("warn", feature, message, detail),
		error: (message, detail) => write("error", feature, message, detail),
	};
}
