import { type FeatureContext, parseJson } from "@oceanadventure/script-framework";

import { Channels, tagName } from "../tags";
import { feedbackTuning } from "../tuning";

/**
 * What the player sees and hears when something happens.
 *
 * Every reaction here is a subscriber to a gameplay message the game already broadcasts, so
 * none of it is wired into the system that raised the event -- adding a reaction to a shot
 * being blocked touches this file and nothing else.
 *
 * Nothing in here decides anything. Reactions run on whichever machine receives the message
 * and are free to be wrong about timing; the moment a reaction needs to change an outcome it
 * belongs in a rule, on the server, with the C++ fallback that implies.
 */
export function installReactions(context: FeatureContext, bridge: UE.OceanAdventureScriptMessageBridge): void {
	warnAboutUnforwardedChannels(context, bridge);

	// Hull fractions a vessel has already been warned about, so a ship sitting at 0.49 does
	// not re-warn on every point of chip damage. Per VM: a reload starts the state clean,
	// which at worst repeats one warning and never misses one.
	const warned = new Map<string, number>();

	bridge.OnGameplayMessage.Add((message: UE.OceanAdventureScriptMessage) => {
		const channel = tagName(message.Channel);

		switch (channel) {
			case Channels.ShotBlocked: {
				// The design forbids silent wall hits: a shot eaten by your own structure has
				// to be visible to the shooter, or nobody ever learns the rule.
				const payload = parseJson(message.PayloadJson) as { Reason?: string } | undefined;
				context.log.log(`shot blocked (${payload?.Reason ?? "unknown"})`);
				break;
			}

			case Channels.VesselState: {
				const vesselKey = UE.OceanAdventureFeedbackScriptLibrary.GetActorName(message.Source);
				const hull = message.Magnitude;
				const crossed = feedbackTuning.hullWarningThresholds.find(
					(threshold) => hull <= threshold && (warned.get(vesselKey) ?? 1) > threshold,
				);

				if (crossed !== undefined) {
					warned.set(vesselKey, crossed);
					context.log.warn(`${vesselKey}: hull down to ${(hull * 100).toFixed(0)}%`);
				}
				break;
			}

			case Channels.CarryFailed:
			case Channels.BuildFailed: {
				// A refusal with no reason is the failure both systems already learned the
				// hard way: the player is told nothing and concludes the game is broken.
				context.log.log(`${channel} refused: ${tagName(message.ReasonTag)}`);
				break;
			}

			default:
				break;
		}
	});

	context.log.log("reactions bound");
}

/**
 * Says so when a channel this file listens for is not forwarded by the C++ bridge.
 *
 * Without this the symptom is a handler that never fires, and there is no way for a script
 * author to tell that apart from "this never happened during my test" -- only the bridge
 * knows which channels exist.
 */
function warnAboutUnforwardedChannels(
	context: FeatureContext,
	bridge: UE.OceanAdventureScriptMessageBridge,
): void {
	const forwarded = new Set<string>();
	const tags = bridge.GetForwardedChannels();

	for (let index = 0; index < tags.Num(); index += 1) {
		forwarded.add(tagName(tags.Get(index)));
	}

	for (const channel of Object.values(Channels)) {
		if (!forwarded.has(channel)) {
			context.log.warn(
				`nothing forwards '${channel}'; a handler for it will never fire. ` +
					"Add it to UOceanAdventureScriptMessageBridge::RegisterForwarders.",
			);
		}
	}
}
