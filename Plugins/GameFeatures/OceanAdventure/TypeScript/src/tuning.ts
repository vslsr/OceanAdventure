/**
 * The numbers. This is the file a designer edits, and editing it costs a `npm run build` and,
 * in the editor, nothing else -- no C++ rebuild, no PIE restart.
 *
 * Everything here is a multiplier or a distance, never an asset and never a rule: what the
 * rules *do* lives next door in combat/ and interaction/, so that a balance pass and a design
 * change stay separate edits with separate review.
 */

export const combatTuning = {
	/**
	 * Damage a player does to their own side.
	 *
	 * Not zero. Friendly fire that does nothing teaches nobody to check their line, and the
	 * naval design already leans on shots being blocked by your own hull being *visible*.
	 */
	friendlyFireScale: 0.25,

	/** Naval shells lose bite past their comfortable range rather than falling off a cliff. */
	falloff: {
		fullDamageCm: 4_000,
		minDamageCm: 14_000,
		minScale: 0.55,
	},

	/**
	 * A hurt target takes slightly more. It rewards committing to a duel instead of trading
	 * pot-shots between two ships, which is the failure state this loop keeps drifting into.
	 */
	finisher: {
		belowHealthFraction: 0.25,
		scale: 1.15,
	},

	/**
	 * A hit that lands always costs something. Falloff plus friendly fire can otherwise
	 * multiply down to a rounding error, and a shell that visibly connects for zero reads as
	 * a bug in the netcode rather than as a rule.
	 */
	minimumDamage: 4,
} as const;

export const feedbackTuning = {
	/** Hull fractions that get a warning when a vessel crosses them downwards. */
	hullWarningThresholds: [0.5, 0.25] as const,

	/** Channel the scripts publish their own telemetry on, for HUD and playtest tooling. */
	telemetryChannel: "oa.script.telemetry",
} as const;
