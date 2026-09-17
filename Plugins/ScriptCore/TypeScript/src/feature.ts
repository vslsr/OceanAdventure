import { makeLogger, type Logger } from "./log";

/**
 * The shape every bundle takes.
 *
 * A bundle runs at boot and again on every hot reload, in a VM that was just built from
 * nothing, so setup has exactly one job: bind this generation's handlers. It must not try to
 * clean up after the previous generation -- there is no previous generation to clean up, the
 * isolate is gone, and the host has already dropped the delegates that pointed into it.
 *
 * The try/catch is the point of the wrapper. Without it, one bad bundle throwing during setup
 * leaves the game in a half-bound state where some rules are the script's and some are C++'s,
 * which is far harder to recognise than no script at all.
 */
export interface FeatureContext {
	readonly name: string;
	readonly log: Logger;
}

export function defineFeature(name: string, setup: (context: FeatureContext) => void): void {
	const log = makeLogger(name);
	const context: FeatureContext = { name, log };

	try {
		setup(context);
		log.log("scripts bound");
	} catch (error) {
		log.error("setup failed; this feature's rules fall back to their C++ defaults", error);
	}
}
