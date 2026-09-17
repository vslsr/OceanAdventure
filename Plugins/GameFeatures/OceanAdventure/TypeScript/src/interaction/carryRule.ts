import type { FeatureContext } from "@oceanadventure/script-framework";

import { FailTags, tag } from "../tags";

/**
 * The last word on a lift that already passed every C++ check.
 *
 * Only ever tightens: C++ consults this on its success path, so a rule here can refuse a
 * pickup and can never grant one the framework refused. That asymmetry is deliberate -- the
 * checks a script must not be able to talk its way past (a mounted gun, a gun somebody is
 * sitting at, a half-built one) stay in C++ where they are reviewed.
 *
 * Unlike the damage rule this one runs on clients too, because the carry ability is
 * LocalPredicted: the client runs the same rule to predict with, the server re-checks, and a
 * client running a tampered copy only misleads itself.
 */
export function installCarryRule(context: FeatureContext, hooks: UE.OceanAdventureScriptHooks): void {
	hooks.InteractionRule.Bind((interaction: UE.OceanAdventureInteractionContext) => {
		const verdict = new UE.OceanAdventureInteractionVerdict();
		verdict.bAllowed = true;
		verdict.DurationScale = 1;

		// An unowned object -- a crate washed up on a beach -- has no team and belongs to
		// whoever reaches it. Only a *contested* object is refused.
		const target = interaction.TargetTeamId;
		const instigator = interaction.InstigatorTeamId;

		if (target >= 0 && instigator >= 0 && target !== instigator) {
			verdict.bAllowed = false;
			verdict.DeniedReason = tag(FailTags.WrongTeam);
			return verdict;
		}

		return verdict;
	});

	context.log.log("carry rule bound");
}
