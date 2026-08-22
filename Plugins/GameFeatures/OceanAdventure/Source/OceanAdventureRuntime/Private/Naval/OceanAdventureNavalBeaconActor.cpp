// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalBeaconActor.h"

#include "Character/LyraHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "Net/UnrealNetwork.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalBeaconActor)

AOceanAdventureNavalBeaconActor::AOceanAdventureNavalBeaconActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// A capture that resolves over 30 seconds does not need a per-frame answer.
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = true;
	SetReplicateMovement(false);

	CaptureVolume = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureVolume"));
	SetRootComponent(CaptureVolume);
	CaptureVolume->SetSphereRadius(CaptureRadius);
	CaptureVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureVolume->SetCollisionObjectType(ECC_WorldDynamic);
	CaptureVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureVolume->SetGenerateOverlapEvents(true);
	CaptureVolume->SetCanEverAffectNavigation(false);

	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(CaptureVolume);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCanEverAffectNavigation(false);
}

void AOceanAdventureNavalBeaconActor::BeginPlay()
{
	Super::BeginPlay();

	if (CaptureVolume)
	{
		CaptureVolume->SetSphereRadius(CaptureRadius);
	}
	BroadcastBeaconState();
}

void AOceanAdventureNavalBeaconActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOceanAdventureNavalBeaconActor, bBeaconActive);
	DOREPLIFETIME(AOceanAdventureNavalBeaconActor, CaptureProgress);
	DOREPLIFETIME(AOceanAdventureNavalBeaconActor, CapturingTeamId);
	DOREPLIFETIME(AOceanAdventureNavalBeaconActor, bContested);
}

void AOceanAdventureNavalBeaconActor::SetBeaconActive(bool bNewActive)
{
	if (!HasAuthority() || bBeaconActive == bNewActive)
	{
		return;
	}

	bBeaconActive = bNewActive;
	SetActorTickEnabled(bNewActive);
	if (!bNewActive)
	{
		CaptureProgress = 0.0f;
		CapturingTeamId = INDEX_NONE;
		bContested = false;
	}

	ForceNetUpdate();
	BroadcastBeaconState();

	UE_LOG(LogOceanAdventure, Display, TEXT("[NavalBeacon] Active=%d"), bNewActive);
}

void AOceanAdventureNavalBeaconActor::CollectOccupyingTeams(TArray<int32>& OutTeams) const
{
	OutTeams.Reset();

	TArray<AActor*> Overlapping;
	CaptureVolume->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (const AActor* Candidate : Overlapping)
	{
		// A downed or dead body does not hold ground.
		const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Candidate);
		if (Health && Health->IsDeadOrDying())
		{
			continue;
		}

		const int32 TeamId = NavalTeam::GetTeamId(Candidate);
		if (NavalTeam::IsValidTeam(TeamId))
		{
			OutTeams.AddUnique(TeamId);
		}
	}
}

void AOceanAdventureNavalBeaconActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority() || !bBeaconActive)
	{
		return;
	}

	TArray<int32> OccupyingTeams;
	CollectOccupyingTeams(OccupyingTeams);

	const bool bWasContested = bContested;
	bContested = OccupyingTeams.Num() > 1;

	if (bContested)
	{
		// Frozen, not reset: the point is to force the fight, not to punish whoever arrived
		// first by wiping their progress the moment an enemy shows up.
	}
	else if (OccupyingTeams.Num() == 1)
	{
		const int32 HoldingTeam = OccupyingTeams[0];
		if (CapturingTeamId != HoldingTeam)
		{
			// A different team taking over starts from scratch rather than inheriting.
			CapturingTeamId = HoldingTeam;
			CaptureProgress = 0.0f;
		}

		CaptureProgress = FMath::Clamp(CaptureProgress + DeltaTime / FMath::Max(1.0f, CaptureSeconds), 0.0f, 1.0f);
		if (CaptureProgress >= 1.0f)
		{
			SetBeaconActive(false);
			OnBeaconCaptured.Broadcast(this, HoldingTeam);
			return;
		}
	}
	else
	{
		CaptureProgress = FMath::Max(0.0f, CaptureProgress - DecayPerSecond * DeltaTime);
		if (CaptureProgress <= 0.0f)
		{
			CapturingTeamId = INDEX_NONE;
		}
	}

	if (bWasContested != bContested || CaptureProgress > 0.0f)
	{
		ForceNetUpdate();
		BroadcastBeaconState();
	}
}

void AOceanAdventureNavalBeaconActor::OnRep_BeaconState()
{
	BroadcastBeaconState();
}

void AOceanAdventureNavalBeaconActor::BroadcastBeaconState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalBeaconMessage Message;
	Message.Beacon = const_cast<AOceanAdventureNavalBeaconActor*>(this);
	Message.bActive = bBeaconActive;
	Message.CaptureProgress = CaptureProgress;
	Message.CapturingTeamId = CapturingTeamId;
	Message.bContested = bContested;
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_Beacon, Message);
}
