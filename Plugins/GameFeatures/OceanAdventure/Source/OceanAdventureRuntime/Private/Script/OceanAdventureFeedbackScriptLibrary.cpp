// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/OceanAdventureFeedbackScriptLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "OceanAdventureRuntimeModule.h"
#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureFeedbackScriptLibrary)

namespace OceanAdventureScriptAssets
{
	/**
	 * Resolves an asset path a script named, loading it if necessary.
	 *
	 * A failed load is logged with the path the script gave, not just "asset not found": the
	 * whole point of a path-addressed asset is that the person who typed it is editing a .ts
	 * file and will never see the C++ call site.
	 */
	template <typename TAsset>
	static TAsset* Resolve(const FString& Path, const TCHAR* Purpose)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}

		const FSoftObjectPath SoftPath(Path);
		TAsset* Asset = Cast<TAsset>(SoftPath.TryLoad());

		if (!Asset)
		{
			UE_LOG(
				LogOceanAdventure,
				Warning,
				TEXT("[Script] %s: could not load '%s'. Check the path and that the asset is kept cooked ")
				TEXT("(a path named only from a script is invisible to the reference graph)."),
				Purpose,
				*Path);
		}

		return Asset;
	}
}

bool UOceanAdventureFeedbackScriptLibrary::PlayCueOnActor(
	AActor* Target, FGameplayTag CueTag, FVector Location, FVector Normal)
{
	if (!Target || !CueTag.IsValid())
	{
		return false;
	}

	UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!AbilitySystem)
	{
		UE_LOG(
			LogOceanAdventure,
			Verbose,
			TEXT("[Script] PlayCueOnActor: %s has no ability system, cue %s skipped"),
			*GetNameSafe(Target),
			*CueTag.ToString());
		return false;
	}

	FGameplayCueParameters Parameters;
	Parameters.Location = Location;
	Parameters.Normal = Normal;
	Parameters.Instigator = Target;

	AbilitySystem->ExecuteGameplayCue(CueTag, Parameters);
	return true;
}

bool UOceanAdventureFeedbackScriptLibrary::SpawnEffectAtLocation(
	const UObject* WorldContextObject,
	const FString& NiagaraSystemPath,
	FVector Location,
	FRotator Rotation,
	float Scale)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return false;
	}

	UNiagaraSystem* System =
		OceanAdventureScriptAssets::Resolve<UNiagaraSystem>(NiagaraSystemPath, TEXT("SpawnEffectAtLocation"));
	if (!System)
	{
		return false;
	}

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, System, Location, Rotation, FVector(Scale), /*bAutoDestroy=*/true);

	return true;
}

bool UOceanAdventureFeedbackScriptLibrary::PlaySoundAtLocation(
	const UObject* WorldContextObject,
	const FString& SoundPath,
	FVector Location,
	float VolumeScale,
	float PitchScale)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return false;
	}

	USoundBase* Sound = OceanAdventureScriptAssets::Resolve<USoundBase>(SoundPath, TEXT("PlaySoundAtLocation"));
	if (!Sound)
	{
		return false;
	}

	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, VolumeScale, PitchScale);
	return true;
}

void UOceanAdventureFeedbackScriptLibrary::BroadcastScriptMessage(
	const UObject* WorldContextObject, FGameplayTag Channel, const FOceanAdventureScriptMessage& Message)
{
	if (!Channel.IsValid())
	{
		UE_LOG(LogOceanAdventure, Warning, TEXT("[Script] BroadcastScriptMessage called with no channel tag"));
		return;
	}

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return;
	}

	FOceanAdventureScriptMessage Outgoing = Message;
	// The channel is what listeners match on, so it is filled in from the argument rather than
	// trusted from the payload: a script that sets one and passes the other would broadcast to
	// listeners that then read a different tag off the struct.
	Outgoing.Channel = Channel;

	UGameplayMessageSubsystem::Get(World).BroadcastMessage(Channel, Outgoing);
}

void UOceanAdventureFeedbackScriptLibrary::ScriptLog(const FString& Message, bool bWarning)
{
	if (bWarning)
	{
		UE_LOG(LogOceanAdventure, Warning, TEXT("[Script] %s"), *Message);
	}
	else
	{
		UE_LOG(LogOceanAdventure, Log, TEXT("[Script] %s"), *Message);
	}
}

FString UOceanAdventureFeedbackScriptLibrary::GetActorName(const AActor* Actor)
{
	return GetNameSafe(Actor);
}
