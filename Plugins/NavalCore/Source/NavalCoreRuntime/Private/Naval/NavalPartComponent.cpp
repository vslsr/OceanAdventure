// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalPartComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Naval/NavalTimeStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "NavalCoreRuntimeModule.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalPartComponent)

UNavalPartComponent::UNavalPartComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Only the construction ramp ticks, and a tenth of a second is finer than a player can
	// read off a durability bar.
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UNavalPartComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveVessel();

	if (GetOwner() && GetOwner()->HasAuthority() && PartState == ENavalPartState::Operational)
	{
		Durability = MaxDurability;
	}

	if (UNavalVesselComponent* Vessel = OwningVessel.Get())
	{
		Vessel->RegisterPart(this);
		Vessel->NotifyPartChanged(this);
	}
}

void UNavalPartComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StateTimerHandle);
	}

	if (UNavalVesselComponent* Vessel = OwningVessel.Get())
	{
		Vessel->UnregisterPart(this);
	}
	OwningVessel.Reset();

	Super::EndPlay(EndPlayReason);
}

void UNavalPartComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalPartComponent, PartState);
	DOREPLIFETIME(UNavalPartComponent, Durability);
}

void UNavalPartComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PartState != ENavalPartState::UnderConstruction || !GetOwner() || !GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	// Durability climbs with construction progress, so a wall raised in front of an inbound
	// shell is a real but fragile play rather than a free block.
	const float Progress = GetConstructionProgress();
	const float RampTarget = FMath::Lerp(MaxDurability * ConstructionStartFraction, MaxDurability, Progress);
	const float NewDurability = FMath::Clamp(RampTarget - ConstructionDamageTaken, 0.0f, MaxDurability);
	if (!FMath::IsNearlyEqual(NewDurability, Durability, 0.01f))
	{
		Durability = NewDurability;
	}
}

void UNavalPartComponent::BeginConstruction()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	ConstructionDamageTaken = 0.0f;

	if (DeploySeconds > 0.0f)
	{
		Durability = 0.0f;
		EnterState(ENavalPartState::Deploying);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				StateTimerHandle, this, &UNavalPartComponent::FinishDeployWindup, DeploySeconds, false);
		}
		return;
	}

	FinishDeployWindup();
}

void UNavalPartComponent::FinishDeployWindup()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (ConstructionSeconds <= 0.0f)
	{
		FinishConstruction();
		return;
	}

	ConstructionStartServerTime = NavalTime::GetNetworkTimeSeconds(this);
	Durability = MaxDurability * ConstructionStartFraction;
	EnterState(ENavalPartState::UnderConstruction);
	SetComponentTickEnabled(true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			StateTimerHandle, this, &UNavalPartComponent::FinishConstruction, ConstructionSeconds, false);
	}
}

void UNavalPartComponent::FinishConstruction()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	SetComponentTickEnabled(false);
	// Damage taken during construction is kept: finishing repairs the schedule, not the holes.
	Durability = FMath::Clamp(MaxDurability - ConstructionDamageTaken, 1.0f, MaxDurability);
	EnterState(ENavalPartState::Operational);
}

void UNavalPartComponent::SetOperationalImmediately()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StateTimerHandle);
	}
	SetComponentTickEnabled(false);
	ConstructionDamageTaken = 0.0f;
	Durability = MaxDurability;
	EnterState(ENavalPartState::Operational);
}

float UNavalPartComponent::ApplyPartDamage(float Amount, AActor* DamageInstigator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.0f)
	{
		return 0.0f;
	}

	// A deploying part is a hologram: no collision, no blocking, nothing to shoot.
	if (PartState == ENavalPartState::Deploying
		|| PartState == ENavalPartState::Destroyed
		|| PartState == ENavalPartState::Disabled)
	{
		return 0.0f;
	}

	const float Applied = FMath::Min(Amount, Durability);
	Durability = FMath::Max(0.0f, Durability - Amount);
	LastDamageServerTime = NavalTime::GetNetworkTimeSeconds(this);
	if (PartState == ENavalPartState::UnderConstruction)
	{
		ConstructionDamageTaken += Applied;
	}

	if (Durability <= 0.0f)
	{
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(StateTimerHandle);
		}
		SetComponentTickEnabled(false);
		EnterState(bDestroyOwnerWhenDisabled ? ENavalPartState::Destroyed : ENavalPartState::Disabled);

		if (bDestroyOwnerWhenDisabled)
		{
			GetOwner()->Destroy();
			return Applied;
		}
	}
	else if (UNavalVesselComponent* Vessel = OwningVessel.Get())
	{
		// Durability changed without a state change; the HUD still wants the new bar.
		Vessel->NotifyPartChanged(this);
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	UE_LOG(
		LogNavalCore,
		Verbose,
		TEXT("[Part] Damage part=%s type=%d applied=%.1f left=%.1f state=%d instigator=%s"),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(PartType),
		Applied,
		Durability,
		static_cast<int32>(PartState),
		*GetNameSafe(DamageInstigator));

	return Applied;
}

bool UNavalPartComponent::RepairPart(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0.0f)
	{
		return false;
	}
	if (PartState == ENavalPartState::Destroyed || PartState == ENavalPartState::Deploying)
	{
		return false;
	}

	// 维修锁定: no healing while the shooting is still landing, which is what stops two
	// players trading an unbreakable wall back and forth.
	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	if (Now - LastDamageServerTime < RepairLockoutSeconds)
	{
		return false;
	}
	if (Durability >= MaxDurability)
	{
		return false;
	}

	Durability = FMath::Min(MaxDurability, Durability + Amount);
	ConstructionDamageTaken = FMath::Max(0.0f, ConstructionDamageTaken - Amount);
	if (PartState == ENavalPartState::Disabled && Durability > 0.0f)
	{
		EnterState(ENavalPartState::Operational);
	}
	else if (UNavalVesselComponent* Vessel = OwningVessel.Get())
	{
		Vessel->NotifyPartChanged(this);
	}

	GetOwner()->ForceNetUpdate();
	return true;
}

float UNavalPartComponent::GetDurabilityFraction() const
{
	return MaxDurability > 0.0f ? FMath::Clamp(Durability / MaxDurability, 0.0f, 1.0f) : 0.0f;
}

bool UNavalPartComponent::IsPhysicallyPresent() const
{
	return PartState == ENavalPartState::UnderConstruction
		|| PartState == ENavalPartState::Operational
		|| PartState == ENavalPartState::Disabled;
}

float UNavalPartComponent::GetConstructionProgress() const
{
	if (PartState == ENavalPartState::Deploying)
	{
		return 0.0f;
	}
	if (PartState != ENavalPartState::UnderConstruction || ConstructionSeconds <= 0.0f)
	{
		return 1.0f;
	}

	const double Elapsed = NavalTime::GetNetworkTimeSeconds(this) - ConstructionStartServerTime;
	return static_cast<float>(FMath::Clamp(Elapsed / ConstructionSeconds, 0.0, 1.0));
}

UNavalVesselComponent* UNavalPartComponent::GetOwningVessel() const
{
	return OwningVessel.Get();
}

void UNavalPartComponent::ConfigureFromFragment(
	ENavalPartType InPartType,
	float InMaxDurability,
	float InDeploySeconds,
	float InConstructionSeconds)
{
	PartType = InPartType;
	MaxDurability = FMath::Max(1.0f, InMaxDurability);
	DeploySeconds = FMath::Max(0.0f, InDeploySeconds);
	ConstructionSeconds = FMath::Max(0.0f, InConstructionSeconds);
}

void UNavalPartComponent::SetLoadContribution(float InTonnage, float InBuoyancy, float InThrust, float InSteering)
{
	TonnageContribution = FMath::Max(0.0f, InTonnage);
	BuoyancyContribution = FMath::Max(0.0f, InBuoyancy);
	ThrustContribution = FMath::Max(0.0f, InThrust);
	SteeringContribution = FMath::Max(0.0f, InSteering);
}

void UNavalPartComponent::EnterState(ENavalPartState NewState)
{
	const bool bChanged = PartState != NewState;
	PartState = NewState;

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	if (bChanged)
	{
		OnPartStateChanged.Broadcast(this);
	}
	BroadcastPartState();
}

void UNavalPartComponent::OnRep_PartState()
{
	OnPartStateChanged.Broadcast(this);
	BroadcastPartState();
}

void UNavalPartComponent::OnRep_Durability()
{
	BroadcastPartState();
}

void UNavalPartComponent::BroadcastPartState() const
{
	if (UNavalVesselComponent* Vessel = OwningVessel.Get())
	{
		Vessel->NotifyPartChanged(const_cast<UNavalPartComponent*>(this));
	}
}

void UNavalPartComponent::ResolveVessel()
{
	OwningVessel = UNavalVesselComponent::FindVessel(GetOwner());
}
