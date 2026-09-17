import { emit, type FeatureContext } from "@oceanadventure/script-framework";

import { combatTuning, feedbackTuning } from "../tuning";

/**
 * Everything that happens to a naval hit between "it connected" and "GAS applies it".
 *
 * The split is the whole point of the integration: C++ still owns whether a shot was fired,
 * whether it was blocked by a wall, what it struck and who is allowed to say so. This file
 * owns only the arithmetic on top of a confirmed hit, which is the part that changes twice a
 * day during a balance pass and never needs a compiler.
 *
 * It must stay a pure function of the context it is handed. No timers, no state carried
 * between hits, no lookups into the world: the rule runs on the server inside impact
 * resolution, and it is re-created from scratch on every hot reload, so anything it
 * remembered would be silently wrong after the next file save.
 */
export function installDamageRule(context: FeatureContext, hooks: UE.OceanAdventureScriptHooks): void {
	if (!hooks.IsAuthority()) {
		// Clients are told, once, instead of silently binding something the host will refuse.
		context.log.log("damage rule not bound: this is a client, damage is resolved on the server");
		return;
	}

	hooks.DamageRule.Bind((damage: UE.OceanAdventureDamageContext) => {
		const verdict = new UE.OceanAdventureDamageVerdict();

		let scale = 1;

		if (damage.bFriendlyFire) {
			scale *= combatTuning.friendlyFireScale;
		}

		scale *= distanceScale(damage.DistanceCm);

		// A negative fraction means the target has no health component at all -- a crate, a
		// gun, a piece of hull. Those are the framework's business, not this rule's, so the
		// finisher bonus stays out of it rather than guessing.
		if (
			damage.TargetHealthFraction >= 0 &&
			damage.TargetHealthFraction <= combatTuning.finisher.belowHealthFraction
		) {
			scale *= combatTuning.finisher.scale;
		}

		const scaled = damage.BaseDamage * scale;
		verdict.Damage = Math.max(scaled, combatTuning.minimumDamage);

		emit(feedbackTuning.telemetryChannel, {
			kind: "damage",
			base: damage.BaseDamage,
			final: verdict.Damage,
			friendly: damage.bFriendlyFire,
			distanceCm: Math.round(damage.DistanceCm),
		});

		return verdict;
	});

	context.log.log("damage rule bound");
}

/**
 * Linear falloff between the two ranges, flat outside them.
 *
 * Clamped at both ends on purpose: a hit closer than fullDamageCm must not scale *above* 1,
 * which an unclamped interpolation would happily do for a point-blank shot and which would
 * turn every boarding action into a one-shot.
 */
export function distanceScale(distanceCm: number): number {
	const { fullDamageCm, minDamageCm, minScale } = combatTuning.falloff;

	// A negative distance means there was no instigator to measure from, e.g. world damage.
	if (distanceCm < 0 || distanceCm <= fullDamageCm) {
		return 1;
	}

	if (distanceCm >= minDamageCm) {
		return minScale;
	}

	const t = (distanceCm - fullDamageCm) / (minDamageCm - fullDamageCm);
	return 1 + (minScale - 1) * t;
}
