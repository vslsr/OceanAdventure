// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/OceanAdventureCombatScriptLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/LyraHealthComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalSettings.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureCombatScriptLibrary)

bool UOceanAdventureCombatScriptLibrary::ApplyCharacterDamage(
	AActor* Target, AActor* Instigator, AActor* Causer, float Damage, FVector ImpactLocation)
{
	if (!Target || Damage <= 0.0f)
	{
		return false;
	}

	const UWorld* World = Target->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[Script] ApplyCharacterDamage refused off the server; damage is server truth."));
		return false;
	}

	UAbilitySystemComponent* TargetAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!TargetAbilitySystem)
	{
		return false;
	}

	const UOceanAdventureNavalSettings& Settings = UOceanAdventureNavalSettings::Get();
	const TSubclassOf<UGameplayEffect> DamageEffect = Settings.ProjectileDamageEffect.LoadSynchronous();
	if (!DamageEffect)
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[Script] ApplyCharacterDamage found no ProjectileDamageEffect configured; %.1f damage to %s was dropped"),
			Damage,
			*GetNameSafe(Target));
		return false;
	}

	UAbilitySystemComponent* SourceAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Instigator);
	UAbilitySystemComponent* EffectSource = SourceAbilitySystem ? SourceAbilitySystem : TargetAbilitySystem;

	FGameplayEffectContextHandle ContextHandle = EffectSource->MakeEffectContext();
	ContextHandle.AddInstigator(Instigator, Causer);
	ContextHandle.AddOrigin(ImpactLocation);

	const FGameplayEffectSpecHandle SpecHandle =
		EffectSource->MakeOutgoingSpec(DamageEffect, /*Level=*/1.0f, ContextHandle);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(Settings.DamageSetByCallerTag, Damage);
	EffectSource->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data, TargetAbilitySystem);

	return true;
}

int32 UOceanAdventureCombatScriptLibrary::GetTeamId(const AActor* Actor)
{
	return NavalTeam::GetTeamId(Actor);
}

bool UOceanAdventureCombatScriptLibrary::AreEnemies(const AActor* A, const AActor* B)
{
	const int32 TeamA = NavalTeam::GetTeamId(A);
	const int32 TeamB = NavalTeam::GetTeamId(B);

	// Unknown is not hostile. A missing team usually means an actor that has not finished
	// possessing yet, and treating that as an enemy makes the first second of a match lethal.
	return NavalTeam::IsValidTeam(TeamA) && NavalTeam::IsValidTeam(TeamB) && TeamA != TeamB;
}

float UOceanAdventureCombatScriptLibrary::GetHealthFraction(const AActor* Actor)
{
	if (const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Actor))
	{
		return Health->GetHealthNormalized();
	}

	return -1.0f;
}

float UOceanAdventureCombatScriptLibrary::GetVesselHullFraction(const AActor* Actor)
{
	if (const UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(Actor))
	{
		return Vessel->GetHullFraction();
	}

	return -1.0f;
}

float UOceanAdventureCombatScriptLibrary::GetDistanceCm(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return -1.0f;
	}

	return FVector::Dist(A->GetActorLocation(), B->GetActorLocation());
}

TArray<AActor*> UOceanAdventureCombatScriptLibrary::FindPawnsInRadius(
	const UObject* WorldContextObject, FVector Center, float RadiusCm, int32 MaxResults)
{
	TArray<AActor*> Result;

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || RadiusCm <= 0.0f)
	{
		return Result;
	}

	const int32 Cap = FMath::Clamp(MaxResults, 1, 64);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Center,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(RadiusCm));

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (Actor && Actor->IsA<APawn>())
		{
			Result.AddUnique(Actor);

			if (Result.Num() >= Cap)
			{
				break;
			}
		}
	}

	return Result;
}

FOceanAdventureDamageContext UOceanAdventureCombatScriptLibrary::MakeDamageContext(
	AActor* Instigator,
	AActor* Target,
	AActor* Causer,
	float BaseDamage,
	FVector ImpactLocation,
	FVector ImpactNormal,
	FGameplayTag SourceTag)
{
	FOceanAdventureDamageContext Context;
	Context.Instigator = Instigator;
	Context.Target = Target;
	Context.Causer = Causer;
	Context.BaseDamage = BaseDamage;
	Context.ImpactLocation = ImpactLocation;
	Context.ImpactNormal = ImpactNormal;
	Context.SourceTag = SourceTag;

	Context.InstigatorTeamId = NavalTeam::GetTeamId(Instigator);
	Context.TargetTeamId = NavalTeam::GetTeamId(Target);
	Context.bFriendlyFire =
		NavalTeam::IsValidTeam(Context.InstigatorTeamId) && Context.InstigatorTeamId == Context.TargetTeamId;

	Context.DistanceCm = Instigator ? FVector::Dist(Instigator->GetActorLocation(), ImpactLocation) : -1.0f;
	Context.TargetHealthFraction = GetHealthFraction(Target);

	return Context;
}
