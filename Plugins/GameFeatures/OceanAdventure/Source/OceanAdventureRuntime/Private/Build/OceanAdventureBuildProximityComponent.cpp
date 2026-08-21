// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureBuildProximityComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureBuildProximityComponent)

UOceanAdventureBuildProximityComponent::UOceanAdventureBuildProximityComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UOceanAdventureBuildProximityComponent::BeginPlay()
{
	Super::BeginPlay();

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	// The build abilities are LocalPredicted, so both the owning client and the server test the
	// tag. Each side keeps its own copy up to date instead of replicating it.
	if (!Pawn->HasAuthority() && !Pawn->IsLocallyControlled())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ScanTimerHandle,
			this,
			&ThisClass::ScanForHost,
			ScanInterval,
			true);
	}
	ScanForHost();
}

void UOceanAdventureBuildProximityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimerHandle);
	}
	SetHostAvailable(false);
	Super::EndPlay(EndPlayReason);
}

void UOceanAdventureBuildProximityComponent::ScanForHost()
{
	const APawn* Pawn = GetPawn<APawn>();
	const UWorld* World = GetWorld();
	UBuildStructureSubsystem* Registry = World
		? World->GetSubsystem<UBuildStructureSubsystem>()
		: nullptr;
	if (!Pawn || !Registry)
	{
		SetHostAvailable(false);
		return;
	}

	// Leaving uses the larger radius, so a player sitting exactly on the boundary does not
	// toggle build mode on and off as the raft rides the swell.
	const double Radius = bHostAvailable ? EnterRadius * ExitRadiusScale : EnterRadius;
	UBuildStructureComponent* Host = Registry->FindNearestStructure(Pawn->GetActorLocation(), Radius);

	NearestHost = Host;
	SetHostAvailable(Host != nullptr);
}

void UOceanAdventureBuildProximityComponent::SetHostAvailable(bool bAvailable)
{
	if (bHostAvailable == bAvailable)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!AbilitySystem)
	{
		// Lyra's ASC lives on the PlayerState and may not have replicated yet. Leave the state
		// unchanged so the next scan retries instead of silently believing the tag is applied.
		return;
	}

	bHostAvailable = bAvailable;
	if (bAvailable)
	{
		AbilitySystem->AddLooseGameplayTag(OceanAdventureBuildTags::Status_Build_HostAvailable);
	}
	else
	{
		AbilitySystem->RemoveLooseGameplayTag(OceanAdventureBuildTags::Status_Build_HostAvailable);
	}
}
