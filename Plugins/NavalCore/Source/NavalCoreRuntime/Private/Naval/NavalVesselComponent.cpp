// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalVesselComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalBuoyancyControl.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalTimeStatics.h"
#include "NavalCoreRuntimeModule.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalVesselComponent)

UNavalVesselComponent::UNavalVesselComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNavalVesselComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		HullDurability = MaxHullDurability;
	}

	if (UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		Registry->RegisterVessel(this);
	}

	BroadcastVesselState();
}

void UNavalVesselComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FounderTimerHandle);
	}
	if (UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		Registry->UnregisterVessel(this);
	}
	Parts.Reset();

	Super::EndPlay(EndPlayReason);
}

void UNavalVesselComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalVesselComponent, VesselState);
	DOREPLIFETIME(UNavalVesselComponent, HullDurability);
	DOREPLIFETIME(UNavalVesselComponent, TeamId);
	DOREPLIFETIME(UNavalVesselComponent, FounderEndServerTime);
	DOREPLIFETIME(UNavalVesselComponent, bEmergencyRepairSpent);
	DOREPLIFETIME(UNavalVesselComponent, bIsLifeRaft);
	DOREPLIFETIME(UNavalVesselComponent, bFounderCountdownPaused);
	DOREPLIFETIME(UNavalVesselComponent, PausedFounderRemaining);
}

UNavalVesselComponent* UNavalVesselComponent::FindVessel(const AActor* Actor)
{
	// A wall instance, a placed cannon or a projectile all resolve to the vessel that carries
	// them. The depth guard keeps a malformed attachment cycle from hanging the server.
	const AActor* Current = Actor;
	for (int32 Depth = 0; Current != nullptr && Depth < 8; ++Depth)
	{
		if (UNavalVesselComponent* Found = Current->FindComponentByClass<UNavalVesselComponent>())
		{
			return Found;
		}

		const AActor* Next = Current->GetAttachParentActor();
		if (!Next)
		{
			Next = Current->GetOwner();
		}
		if (Next == Current)
		{
			break;
		}
		Current = Next;
	}

	return nullptr;
}

void UNavalVesselComponent::SetTeamId(int32 NewTeamId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || TeamId == NewTeamId)
	{
		return;
	}

	TeamId = NewTeamId;
	GetOwner()->ForceNetUpdate();
	BroadcastVesselState();
}

float UNavalVesselComponent::GetHullFraction() const
{
	return MaxHullDurability > 0.0f ? FMath::Clamp(HullDurability / MaxHullDurability, 0.0f, 1.0f) : 0.0f;
}

float UNavalVesselComponent::GetFounderSecondsRemaining() const
{
	if (VesselState != ENavalVesselState::Foundering)
	{
		return 0.0f;
	}
	if (bFounderCountdownPaused)
	{
		return FMath::Max(0.0f, PausedFounderRemaining);
	}

	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	return static_cast<float>(FMath::Max(0.0, FounderEndServerTime - Now));
}

bool UNavalVesselComponent::IsEmergencyRepairAvailable() const
{
	return !bEmergencyRepairSpent;
}

float UNavalVesselComponent::ApplyHullDamage(float Amount, AActor* DamageInstigator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.0f)
	{
		return 0.0f;
	}

	if (VesselState == ENavalVesselState::Wreck)
	{
		return 0.0f;
	}

	if (VesselState == ENavalVesselState::Foundering)
	{
		// The hull is already at zero. Continuing to shoot is still the fast, safe kill: it
		// eats the countdown the defenders were going to repair inside of.
		if (!bFounderCountdownPaused && FounderSeconds > 0.0f && MaxHullDurability > 0.0f)
		{
			const double Now = NavalTime::GetNetworkTimeSeconds(this);
			const double Shortening = FounderSeconds * (Amount / MaxHullDurability);
			FounderEndServerTime = FMath::Max(Now, FounderEndServerTime - Shortening);
			ScheduleFounderTimer();
			GetOwner()->ForceNetUpdate();
			BroadcastVesselState();
		}
		return Amount;
	}

	const float Applied = FMath::Min(Amount, HullDurability);
	HullDurability = FMath::Max(0.0f, HullDurability - Amount);
	GetOwner()->ForceNetUpdate();

	if (HullDurability <= 0.0f)
	{
		BeginFoundering(DamageInstigator);
	}
	else
	{
		BroadcastVesselState();
	}

	return Applied;
}

bool UNavalVesselComponent::RepairHull(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.0f)
	{
		return false;
	}

	// Repairing a hull that already reached zero is the emergency-repair path, not this one.
	if (VesselState != ENavalVesselState::Operational || HullDurability >= MaxHullDurability)
	{
		return false;
	}

	HullDurability = FMath::Min(MaxHullDurability, HullDurability + Amount);
	GetOwner()->ForceNetUpdate();
	BroadcastVesselState();
	return true;
}

bool UNavalVesselComponent::CanBeginEmergencyRepair(const AActor* Repairer, FGameplayTag& OutFailReason) const
{
	if (VesselState != ENavalVesselState::Foundering)
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}
	if (bEmergencyRepairSpent)
	{
		OutFailReason = NavalGameplayTags::Fail_RepairSpent;
		return false;
	}
	if (!Repairer)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (NavalTeam::IsValidTeam(TeamId) && NavalTeam::GetTeamId(Repairer) != TeamId)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (const AActor* OwnerActor = GetOwner())
	{
		const double DistanceSquared = FVector::DistSquared(
			Repairer->GetActorLocation(), OwnerActor->GetActorLocation());
		if (DistanceSquared > FMath::Square(static_cast<double>(EmergencyRepairRange)))
		{
			OutFailReason = NavalGameplayTags::Fail_TooFar;
			return false;
		}
	}

	return true;
}

bool UNavalVesselComponent::CommitEmergencyRepair(AActor* Repairer)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	// The client asked for this after channelling; the server re-runs the same check because
	// the world moved while the channel was running.
	FGameplayTag FailReason;
	if (!CanBeginEmergencyRepair(Repairer, FailReason))
	{
		UE_LOG(
			LogNavalCore,
			Verbose,
			TEXT("[Vessel] Emergency repair refused vessel=%s repairer=%s reason=%s"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Repairer),
			*FailReason.ToString());
		return false;
	}

	bEmergencyRepairSpent = true;
	bFounderCountdownPaused = false;
	PausedFounderRemaining = 0.0f;
	FounderEndServerTime = 0.0;
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FounderTimerHandle);
	}

	HullDurability = FMath::Max(1.0f, MaxHullDurability * EmergencyRepairHullFraction);
	SetHostBuoyancyEnabled(true);
	EnterState(ENavalVesselState::Operational);
	return true;
}

bool UNavalVesselComponent::SetFounderCountdownPaused(bool bPaused)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || VesselState != ENavalVesselState::Foundering)
	{
		return false;
	}
	if (bFounderCountdownPaused == bPaused)
	{
		return true;
	}

	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	if (bPaused)
	{
		PausedFounderRemaining = static_cast<float>(FMath::Max(0.0, FounderEndServerTime - Now));
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FounderTimerHandle);
		}
	}
	else
	{
		FounderEndServerTime = Now + FMath::Max(0.0f, PausedFounderRemaining);
		PausedFounderRemaining = 0.0f;
		ScheduleFounderTimer();
	}

	bFounderCountdownPaused = bPaused;
	GetOwner()->ForceNetUpdate();
	BroadcastVesselState();
	return true;
}

void UNavalVesselComponent::ForceWreck()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		HandleFounderElapsed();
	}
}

void UNavalVesselComponent::ConfigureAsLifeRaft(float HullScale)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bIsLifeRaft = true;
	MaxHullDurability = FMath::Max(1.0f, MaxHullDurability * FMath::Clamp(HullScale, 0.05f, 1.0f));
	HullDurability = MaxHullDurability;
	// A raft the crew was handed after losing their ship starts with its one save unused.
	bEmergencyRepairSpent = false;
	GetOwner()->ForceNetUpdate();
	BroadcastVesselState();
}

void UNavalVesselComponent::RegisterPart(UNavalPartComponent* Part)
{
	if (!Part)
	{
		return;
	}

	Parts.AddUnique(Part);
	OnVesselPartsChanged.Broadcast(this);
}

void UNavalVesselComponent::UnregisterPart(UNavalPartComponent* Part)
{
	if (!Part)
	{
		return;
	}

	const int32 RemovedCount = Parts.RemoveAll(
		[Part](const TWeakObjectPtr<UNavalPartComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Part;
		});

	if (RemovedCount > 0)
	{
		OnVesselPartsChanged.Broadcast(this);
	}
}

void UNavalVesselComponent::NotifyPartChanged(UNavalPartComponent* Part)
{
	OnVesselPartsChanged.Broadcast(this);

	if (!Part)
	{
		return;
	}

	FNavalPartStateMessage Message;
	Message.Vessel = GetOwner();
	Message.PartActor = Part->GetOwner();
	Message.PartType = Part->GetPartType();
	Message.PartState = Part->GetPartState();
	Message.DurabilityFraction = Part->GetDurabilityFraction();
	if (const UWorld* World = GetWorld())
	{
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(
			NavalGameplayTags::Message_Vessel_Part, Message);
	}
}

float UNavalVesselComponent::GetPartCapability(ENavalPartType QueryType) const
{
	int32 TotalCount = 0;
	int32 FunctionalCount = 0;
	for (const TWeakObjectPtr<UNavalPartComponent>& Entry : Parts)
	{
		const UNavalPartComponent* Part = Entry.Get();
		if (!Part || Part->GetPartType() != QueryType)
		{
			continue;
		}

		++TotalCount;
		if (Part->IsFunctional())
		{
			++FunctionalCount;
		}
	}

	// No part of this type was ever installed, so there is nothing to have lost: the caller's
	// own baseline applies. A T0 raft has no separate thrusters and still moves.
	if (TotalCount == 0)
	{
		return 1.0f;
	}

	return static_cast<float>(FunctionalCount) / static_cast<float>(TotalCount);
}

void UNavalVesselComponent::BeginFoundering(AActor* DamageInstigator)
{
	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	FounderEndServerTime = Now + FMath::Max(1.0f, FounderSeconds);
	bFounderCountdownPaused = false;
	PausedFounderRemaining = 0.0f;
	ScheduleFounderTimer();
	EnterState(ENavalVesselState::Foundering);

	FNavalAlertMessage Alert;
	Alert.Vessel = GetOwner();
	Alert.AlertTag = NavalGameplayTags::Alert_Foundering;
	Alert.WorldLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	Alert.TeamId = TeamId;
	if (const UWorld* World = GetWorld())
	{
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(
			NavalGameplayTags::Message_Vessel_Alert, Alert);
	}

	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[Vessel] Hull reached zero vessel=%s team=%d instigator=%s founder_seconds=%.1f"),
		*GetNameSafe(GetOwner()),
		TeamId,
		*GetNameSafe(DamageInstigator),
		FounderSeconds);
}

void UNavalVesselComponent::ScheduleFounderTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	const float Delay = static_cast<float>(FMath::Max(0.0, FounderEndServerTime - Now));
	World->GetTimerManager().SetTimer(
		FounderTimerHandle, this, &UNavalVesselComponent::HandleFounderElapsed, FMath::Max(0.01f, Delay), false);
}

void UNavalVesselComponent::HandleFounderElapsed()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || VesselState == ENavalVesselState::Wreck)
	{
		return;
	}

	HullDurability = 0.0f;
	// A wreck stops being held at the waterline; the host decides what sinking looks like.
	SetHostBuoyancyEnabled(false);
	EnterState(ENavalVesselState::Wreck);
}

void UNavalVesselComponent::EnterState(ENavalVesselState NewState)
{
	if (VesselState == NewState)
	{
		BroadcastVesselState();
		return;
	}

	VesselState = NewState;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	OnVesselStateChanged.Broadcast(this, NewState);
	BroadcastVesselState();
}

void UNavalVesselComponent::OnRep_VesselState()
{
	OnVesselStateChanged.Broadcast(this, VesselState);
	BroadcastVesselState();
}

void UNavalVesselComponent::OnRep_HullDurability()
{
	BroadcastVesselState();
}

void UNavalVesselComponent::BroadcastVesselState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FNavalVesselStateMessage Message;
	Message.Vessel = GetOwner();
	Message.State = VesselState;
	Message.HullFraction = GetHullFraction();
	Message.FounderSecondsRemaining = GetFounderSecondsRemaining();
	Message.TeamId = TeamId;
	Message.bEmergencyRepairAvailable = IsEmergencyRepairAvailable();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		NavalGameplayTags::Message_Vessel_State, Message);
}

void UNavalVesselComponent::SetHostBuoyancyEnabled(bool bEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (INavalBuoyancyControl* ActorControl = Cast<INavalBuoyancyControl>(OwnerActor))
	{
		ActorControl->SetBuoyancyEnabled(bEnabled);
	}

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		if (INavalBuoyancyControl* Control = Cast<INavalBuoyancyControl>(Component))
		{
			Control->SetBuoyancyEnabled(bEnabled);
		}
	}
}
