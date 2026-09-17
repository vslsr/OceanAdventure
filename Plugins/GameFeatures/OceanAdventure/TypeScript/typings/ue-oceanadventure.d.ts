/**
 * The Ocean Adventure half of the script boundary, hand-written.
 *
 * Same contract as ue-core.d.ts in ScriptCore: this is the declaration of what scripts in
 * this feature may reach, kept in step with the C++ by tools/check-api-parity.mjs. Every
 * entry here has a UFUNCTION or a UPROPERTY behind it; a call this file invents would
 * typecheck and then fail at runtime with nothing to point at.
 *
 * It lives inside the feature, like the rest of the feature's content, because the classes it
 * describes do. Another GameFeature adding scripts writes its own; it never edits this one.
 */

declare namespace UE {
	/** Everything a damage rule is allowed to see about one hit. */
	class OceanAdventureDamageContext {
		Instigator: Actor;
		Target: Actor;
		Causer: Actor;
		BaseDamage: number;
		ImpactLocation: Vector;
		ImpactNormal: Vector;
		DistanceCm: number;
		InstigatorTeamId: number;
		TargetTeamId: number;
		bFriendlyFire: boolean;
		SourceTag: GameplayTag;
		TargetHealthFraction: number;
	}

	class OceanAdventureDamageVerdict {
		Damage: number;
		bCancelled: boolean;
		ReasonTag: GameplayTag;
		ImpactCueTag: GameplayTag;
	}

	class OceanAdventureInteractionContext {
		Instigator: Actor;
		Target: Actor;
		InteractionTag: GameplayTag;
		WorldLocation: Vector;
		InstigatorTeamId: number;
		TargetTeamId: number;
	}

	class OceanAdventureInteractionVerdict {
		bAllowed: boolean;
		DeniedReason: GameplayTag;
		CueTag: GameplayTag;
		DurationScale: number;
	}

	class OceanAdventureScriptMessage {
		Channel: GameplayTag;
		Source: Actor;
		Target: Actor;
		WorldLocation: Vector;
		Magnitude: number;
		ReasonTag: GameplayTag;
		PayloadJson: string;
	}

	class OceanAdventureScriptHooks extends Object {
		static Get(WorldContextObject: Object): OceanAdventureScriptHooks;

		DamageRule: ScriptDelegate<(Context: OceanAdventureDamageContext) => OceanAdventureDamageVerdict>;
		InteractionRule: ScriptDelegate<
			(Context: OceanAdventureInteractionContext) => OceanAdventureInteractionVerdict
		>;

		SetDamageRule(Rule: never): void;
		ClearDamageRule(): void;
		HasDamageRule(): boolean;
		SetInteractionRule(Rule: never): void;
		ClearInteractionRule(): void;
		HasInteractionRule(): boolean;
		IsAuthority(): boolean;
	}

	class OceanAdventureScriptMessageBridge extends Object {
		static Get(WorldContextObject: Object): OceanAdventureScriptMessageBridge;

		OnGameplayMessage: ScriptMulticastDelegate<(Message: OceanAdventureScriptMessage) => void>;

		GetForwardedChannels(): TArray<GameplayTag>;
	}

	class OceanAdventureCombatScriptLibrary extends Object {
		static ApplyCharacterDamage(
			Target: Actor,
			Instigator: Actor,
			Causer: Actor,
			Damage: number,
			ImpactLocation: Vector,
		): boolean;
		static GetTeamId(Actor: Actor): number;
		static AreEnemies(A: Actor, B: Actor): boolean;
		static GetHealthFraction(Actor: Actor): number;
		static GetVesselHullFraction(Actor: Actor): number;
		static GetDistanceCm(A: Actor, B: Actor): number;
		static FindPawnsInRadius(
			WorldContextObject: Object,
			Center: Vector,
			RadiusCm: number,
			MaxResults: number,
		): TArray<Actor>;
		static MakeDamageContext(
			Instigator: Actor,
			Target: Actor,
			Causer: Actor,
			BaseDamage: number,
			ImpactLocation: Vector,
			ImpactNormal: Vector,
			SourceTag: GameplayTag,
		): OceanAdventureDamageContext;
	}

	class OceanAdventureFeedbackScriptLibrary extends Object {
		static PlayCueOnActor(Target: Actor, CueTag: GameplayTag, Location: Vector, Normal: Vector): boolean;
		static SpawnEffectAtLocation(
			WorldContextObject: Object,
			NiagaraSystemPath: string,
			Location: Vector,
			Rotation: Rotator,
			Scale: number,
		): boolean;
		static PlaySoundAtLocation(
			WorldContextObject: Object,
			SoundPath: string,
			Location: Vector,
			VolumeScale: number,
			PitchScale: number,
		): boolean;
		static BroadcastScriptMessage(
			WorldContextObject: Object,
			Channel: GameplayTag,
			Message: OceanAdventureScriptMessage,
		): void;
		static ScriptLog(Message: string, bWarning: boolean): void;
		static GetActorName(Actor: Actor): string;
	}

	class OceanAdventureScriptTagLibrary extends Object {
		static MakeTag(TagName: string): GameplayTag;
		static TagToString(Tag: GameplayTag): string;
		static IsTagValid(Tag: GameplayTag): boolean;
		static TagMatches(Tag: GameplayTag, Parent: GameplayTag): boolean;
	}
}
