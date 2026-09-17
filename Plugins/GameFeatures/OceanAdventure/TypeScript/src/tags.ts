/**
 * Gameplay tags, by name, resolved once per VM.
 *
 * Every string here is a tag that already exists in C++. A script cannot register one -- the
 * lookup returns an invalid tag and warns -- which is the behaviour we want: a refusal reason
 * invented in TypeScript would match nothing the HUD, the audio or the playtest log listens
 * for, and would look exactly like a rule that never ran.
 */

const resolved = new Map<string, UE.GameplayTag>();

export function tag(name: string): UE.GameplayTag {
	const cached = resolved.get(name);
	if (cached !== undefined) {
		return cached;
	}

	const value = UE.OceanAdventureScriptTagLibrary.MakeTag(name);
	resolved.set(name, value);
	return value;
}

export function tagName(value: UE.GameplayTag): string {
	return UE.OceanAdventureScriptTagLibrary.TagToString(value);
}

/** Channels the C++ bridge forwards. Names, not tags: tags are resolved on first use. */
export const Channels = {
	ProjectileImpact: "Naval.Message.Projectile.Impact",
	VesselState: "Naval.Message.Vessel.State",
	VesselPart: "Naval.Message.Vessel.Part",
	ShotBlocked: "Naval.Message.Shot.Blocked",
	HeavyWeaponState: "Naval.Message.HeavyWeapon.State",
	CarryFailed: "Carry.Message.Failed",
	BuildFailed: "Build.Message.Failed",
} as const;

/** Refusal reasons a rule here is allowed to give, all of them already defined in C++. */
export const FailTags = {
	WrongTeam: "Naval.Fail.WrongTeam",
	NotOperational: "Naval.Fail.NotOperational",
	CarryInvalid: "Carry.Fail.Invalid",
} as const;
