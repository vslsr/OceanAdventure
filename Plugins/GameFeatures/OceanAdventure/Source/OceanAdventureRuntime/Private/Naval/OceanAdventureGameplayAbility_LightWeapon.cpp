// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_LightWeapon.h"

#include "CollisionShape.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalFireWindowComponent.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_LightWeapon)

UOceanAdventureGameplayAbility_LightWeapon::UOceanAdventureGameplayAbility_LightWeapon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOceanAdventureGameplayAbility_LightWeapon::AddAdditionalTraceIgnoreActors(
	FCollisionQueryParams& TraceParams) const
{
	Super::AddAdditionalTraceIgnoreActors(TraceParams);

	const APawn* Avatar = Cast<APawn>(GetAvatarActorFromActorInfo());
	const UWorld* World = GetWorld();
	if (!Avatar || !World)
	{
		return;
	}

	const FVector ShotStart = Avatar->GetActorLocation();
	// Aim direction from the controller rather than the pawn: in top-down the character faces
	// where the player is aiming, and this has to match the trace Lyra is about to run.
	FVector ShotDirection = Avatar->GetActorForwardVector();
	if (const AController* Controller = Avatar->GetController())
	{
		ShotDirection = Controller->GetControlRotation().Vector();
	}

	const int32 ShooterTeam = NavalTeam::GetTeamId(Avatar);
	if (!NavalTeam::IsValidTeam(ShooterTeam))
	{
		return;
	}

	const float SearchRadius = UOceanAdventureNavalSettings::Get().FriendlyWindowSearchRadius;

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		ShotStart,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(SearchRadius));

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Candidate = Overlap.GetActor();
		const UNavalFireWindowComponent* Window = Candidate
			? Candidate->FindComponentByClass<UNavalFireWindowComponent>()
			: nullptr;
		if (Window && Window->AllowsShot(ShotStart, ShotDirection, ShooterTeam))
		{
			// Only this shot, only this window. A window that would refuse the shot is left
			// in the trace and stops it, exactly like the wall it is part of.
			TraceParams.AddIgnoredActor(Candidate);
		}
	}
}

void UOceanAdventureGameplayAbility_LightWeapon::ApplyNavalHitEffects(
	const FGameplayAbilityTargetDataHandle& TargetData)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority())
	{
		return;
	}

	const UOceanAdventureNavalSettings& Settings = UOceanAdventureNavalSettings::Get();
	const float HitDamage = BaseHitDamage > 0.0f ? BaseHitDamage : Settings.LightWeaponBaseDamage;

	for (int32 Index = 0; Index < TargetData.Num(); ++Index)
	{
		const FGameplayAbilityTargetData* Entry = TargetData.Get(Index);
		const FHitResult* Hit = Entry ? Entry->GetHitResult() : nullptr;
		AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
		if (!HitActor)
		{
			continue;
		}

		// A light weapon is a fallback way to break something, not a siege tool: 20-35% of
		// its damage against structure, less again against a hull.
		if (UNavalPartComponent* Part = HitActor->FindComponentByClass<UNavalPartComponent>())
		{
			Part->ApplyPartDamage(HitDamage * Settings.LightWeaponStructureDamageScale, GetAvatarActorFromActorInfo());
		}
		else if (UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(HitActor))
		{
			Vessel->ApplyHullDamage(HitDamage * Settings.LightWeaponHullDamageScale, GetAvatarActorFromActorInfo());
		}
	}
}
