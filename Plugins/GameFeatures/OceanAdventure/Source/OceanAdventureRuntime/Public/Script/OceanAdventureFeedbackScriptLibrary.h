// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Script/OceanAdventureScriptTypes.h"

#include "OceanAdventureFeedbackScriptLibrary.generated.h"

class AActor;

/**
 * The interaction-and-feedback half of the interface TypeScript calls down through: what a
 * player sees, hears and is told when something happens.
 *
 * Two of these take an asset *path* rather than a typed reference, which is unusual enough in
 * this codebase to justify itself. Feedback is the layer that changes most often and matters
 * least structurally -- swapping which burst plays on a hull hit should not be a C++ edit, a
 * rebuild and an editor restart. The costs are real and are why the rest of the codebase does
 * not work this way: a path is not checked by the cooker's reference graph, so an asset only
 * reachable from a script must be kept loadable by the feature's asset manager rules, and a
 * typo fails at runtime instead of at compile time. Both are logged loudly rather than
 * swallowed. Anything gameplay-critical stays a typed reference in C++ or in a DataAsset.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureFeedbackScriptLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Executes a GameplayCue on an actor's ability system, which is the replicated path: the
	 * server calls it once and every client plays it.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Feedback")
	static bool PlayCueOnActor(AActor* Target, FGameplayTag CueTag, FVector Location, FVector Normal);

	/**
	 * Spawns a Niagara system by asset path. Cosmetic and local: it runs where it is called,
	 * so call it from a rule that already runs on every machine, or pair it with a message.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Feedback", meta = (WorldContext = "WorldContextObject"))
	static bool SpawnEffectAtLocation(
		const UObject* WorldContextObject,
		const FString& NiagaraSystemPath,
		FVector Location,
		FRotator Rotation,
		float Scale = 1.0f);

	/** Plays a sound by asset path at a world location. Cosmetic and local, as above. */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Feedback", meta = (WorldContext = "WorldContextObject"))
	static bool PlaySoundAtLocation(
		const UObject* WorldContextObject,
		const FString& SoundPath,
		FVector Location,
		float VolumeScale = 1.0f,
		float PitchScale = 1.0f);

	/**
	 * Publishes on the gameplay message bus so HUD, audio and telemetry can each subscribe.
	 *
	 * This is how a script tells the player something. Not a client RPC: a refusal reason that
	 * only the HUD hears cannot also be logged by a playtest tool or spoken by a tutorial.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Feedback", meta = (WorldContext = "WorldContextObject"))
	static void BroadcastScriptMessage(
		const UObject* WorldContextObject,
		FGameplayTag Channel,
		const FOceanAdventureScriptMessage& Message);

	/** Writes to LogOceanAdventure under a [Script] prefix, so script output is greppable. */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Feedback")
	static void ScriptLog(const FString& Message, bool bWarning = false);

	/**
	 * An actor's name, for log lines and for keying a script's own bookkeeping.
	 *
	 * Exposed because UObject::GetName is not reflected, so without it a script has no stable
	 * way to say *which* ship it just warned about -- and keying off the wrapper object
	 * instead would depend on how a given VM release caches those.
	 */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Feedback")
	static FString GetActorName(const AActor* Actor);
};
