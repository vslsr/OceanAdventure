import { defineFeature, getEnv } from "@oceanadventure/script-framework";

import { installDamageRule } from "./combat/damageRule";
import { installCarryRule } from "./interaction/carryRule";
import { installReactions } from "./interaction/reactions";

/**
 * Entry point for the Ocean Adventure feature's scripts.
 *
 * The host runs this once per map load and again on every hot reload, in a VM built from
 * nothing each time. So this file binds and nothing else: no unbinding, no migration, no
 * "have I run before" flag. The previous generation's closures are already gone and the host
 * has already dropped the delegates that pointed at them.
 */
defineFeature("OceanAdventure", (context) => {
	const env = getEnv();
	if (!env) {
		context.log.error("no script environment; nothing can be bound");
		return;
	}

	// The environment doubles as the world context: it is a game instance subsystem, so it
	// resolves to the world that was just loaded. That is also why the host boots scripts on
	// map load rather than at startup -- before a world exists these lookups all return null.
	const hooks = UE.OceanAdventureScriptHooks.Get(env);
	const bridge = UE.OceanAdventureScriptMessageBridge.Get(env);

	if (!hooks || !bridge) {
		context.log.error(
			"the Ocean Adventure script subsystems are missing from this world; " +
				"the feature is probably not active in the running Experience",
		);
		return;
	}

	installDamageRule(context, hooks);
	installCarryRule(context, hooks);
	installReactions(context, bridge);
});
