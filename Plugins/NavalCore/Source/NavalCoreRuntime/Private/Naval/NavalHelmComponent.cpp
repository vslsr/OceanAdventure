// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHelmComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHelmActor.h"
#include "Naval/NavalMessages.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalTimeStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "NavalCoreRuntimeModule.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalHelmComponent)

UNavalHelmComponent::UNavalHelmComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Only capture progress needs this tick, and it is a multi-second interaction.
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);

	// The base console works with no authored asset, so a vessel always has a helm to take.
	HelmActorClass = ANavalHelmActor::StaticClass();
}

void UNavalHelmComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SpawnHelmActor();

		if (UWorld* World = GetWorld(); World && OrphanCheckInterval > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				OrphanCheckTimerHandle,
				this,
				&UNavalHelmComponent::CheckOperatorStillControlled,
				OrphanCheckInterval,
				/*bLoop=*/true);
		}
	}

	BroadcastHelmState();
}

void UNavalHelmComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindOperatorDestroyed();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OrphanCheckTimerHandle);
	}

	if (GetOwner() && GetOwner()->HasAuthority() && HelmActor)
	{
		HelmActor->Destroy();
	}
	HelmActor = nullptr;
	Operator = nullptr;
	CaptureChallenger = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UNavalHelmComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalHelmComponent, Operator);
	DOREPLIFETIME(UNavalHelmComponent, HelmActor);
	DOREPLIFETIME(UNavalHelmComponent, CaptureProgress);
	DOREPLIFETIME(UNavalHelmComponent, CapturingTeamId);
}

void UNavalHelmComponent::SpawnHelmActor()
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World || !HelmActorClass || HelmActor)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = OwnerActor;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform HostSpace = OwnerActor->GetActorTransform();
	const FTransform LocalTransform(FRotator(0.0f, HelmLocalYaw, 0.0f), HelmLocalOffset);
	HelmActor = World->SpawnActor<ANavalHelmActor>(
		HelmActorClass, LocalTransform * HostSpace, SpawnParameters);
	if (!HelmActor)
	{
		UE_LOG(LogNavalCore, Error, TEXT("[Helm] Failed to spawn helm actor for vessel=%s"), *GetNameSafe(OwnerActor));
		return;
	}

	// The helm rides the deck; it is never re-placed, which is what makes "you cannot move
	// or dismantle the core at sea" a structural fact rather than a rule players must obey.
	HelmActor->AttachToActor(OwnerActor, FAttachmentTransformRules::KeepWorldTransform);
	OwnerActor->ForceNetUpdate();
}

bool UNavalHelmComponent::CanOccupy(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	if (!Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}

	const UNavalVesselComponent* VesselComponent = GetVessel();
	if (VesselComponent && VesselComponent->GetVesselState() == ENavalVesselState::Wreck)
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	// Standing on an enemy deck is boarding, not ownership: only a completed flag change
	// hands over the wheel.
	if (VesselComponent && NavalTeam::IsValidTeam(VesselComponent->GetTeamId())
		&& NavalTeam::GetTeamId(Candidate) != VesselComponent->GetTeamId())
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}

	if (Operator != nullptr && Operator != Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_SeatOccupied;
		return false;
	}

	const double DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetHelmWorldLocation());
	if (DistanceSquared > FMath::Square(static_cast<double>(InteractionRange)))
	{
		OutFailReason = NavalGameplayTags::Fail_TooFar;
		return false;
	}

	return true;
}

bool UNavalHelmComponent::TryOccupy(AActor* NewOperator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	if (!CanOccupy(NewOperator, FailReason))
	{
		UE_LOG(
			LogNavalCore,
			Verbose,
			TEXT("[Helm] Occupy refused vessel=%s candidate=%s reason=%s"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(NewOperator),
			*FailReason.ToString());
		return false;
	}

	UnbindOperatorDestroyed();
	Operator = NewOperator;
	OperatorLostControllerTime = 0.0;
	BindOperatorDestroyed(NewOperator);
	ThrottleIntent = 0.0f;
	SteerIntent = 0.0f;
	GetOwner()->ForceNetUpdate();
	OnHelmChanged.Broadcast(this);
	BroadcastHelmState();
	return true;
}

void UNavalHelmComponent::ReleaseHelm(AActor* LeavingOperator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (LeavingOperator != nullptr && Operator != LeavingOperator)
	{
		return;
	}

	UnbindOperatorDestroyed();
	Operator = nullptr;
	OperatorLostControllerTime = 0.0;
	// Design 8.3.1: the ship keeps its heading and coasts down. No autopilot, no snap to zero.
	ThrottleIntent = 0.0f;
	SteerIntent = 0.0f;
	GetOwner()->ForceNetUpdate();
	OnHelmChanged.Broadcast(this);
	BroadcastHelmState();
}

void UNavalHelmComponent::BindOperatorDestroyed(AActor* NewOperator)
{
	if (IsValid(NewOperator))
	{
		NewOperator->OnDestroyed.AddDynamic(this, &UNavalHelmComponent::HandleOperatorDestroyed);
	}
}

void UNavalHelmComponent::UnbindOperatorDestroyed()
{
	AActor* CurrentOperator = Operator.Get();
	if (IsValid(CurrentOperator))
	{
		CurrentOperator->OnDestroyed.RemoveDynamic(this, &UNavalHelmComponent::HandleOperatorDestroyed);
	}
}

void UNavalHelmComponent::HandleOperatorDestroyed(AActor* DestroyedActor)
{
	// The same exit the ability uses, so ForceNetUpdate, OnHelmChanged and the helm message
	// all fire exactly as they would if the player had stepped off the wheel themselves.
	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[Helm] Operator destroyed vessel=%s operator=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(DestroyedActor));
	ReleaseHelm(DestroyedActor);
}

void UNavalHelmComponent::CheckOperatorStillControlled()
{
	const AActor* OwnerActor = GetOwner();
	AActor* CurrentOperator = Operator.Get();
	if (!OwnerActor || !OwnerActor->HasAuthority() || CurrentOperator == nullptr)
	{
		OperatorLostControllerTime = 0.0;
		return;
	}

	if (!IsValid(CurrentOperator))
	{
		ReleaseHelm(CurrentOperator);
		return;
	}

	const APawn* OperatorPawn = Cast<APawn>(CurrentOperator);
	if (!OperatorPawn || OperatorPawn->GetController() != nullptr)
	{
		OperatorLostControllerTime = 0.0;
		return;
	}

	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	if (OperatorLostControllerTime <= 0.0)
	{
		OperatorLostControllerTime = Now;
		return;
	}
	if (Now - OperatorLostControllerTime >= static_cast<double>(OrphanGraceSeconds))
	{
		UE_LOG(
			LogNavalCore,
			Display,
			TEXT("[Helm] Operator lost its controller, freeing the wheel vessel=%s operator=%s"),
			*GetNameSafe(OwnerActor),
			*GetNameSafe(CurrentOperator));
		ReleaseHelm(CurrentOperator);
	}
}

void UNavalHelmComponent::SetControlIntent(AActor* Source, float InThrottle, float InSteer)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// The client sends this as a request every frame it holds a direction; only the actor the
	// server believes is at the wheel can move the ship.
	if (Source == nullptr || Operator != Source || !AcceptsControlInput())
	{
		return;
	}

	ThrottleIntent = FMath::Clamp(InThrottle, -1.0f, 1.0f);
	SteerIntent = FMath::Clamp(InSteer, -1.0f, 1.0f);
}

float UNavalHelmComponent::GetThrottleIntent() const
{
	return AcceptsControlInput() ? ThrottleIntent : 0.0f;
}

float UNavalHelmComponent::GetSteerIntent() const
{
	return AcceptsControlInput() ? SteerIntent : 0.0f;
}

bool UNavalHelmComponent::AcceptsControlInput() const
{
	const ENavalHelmState State = GetHelmState();
	if (State == ENavalHelmState::CoreDisabled || State == ENavalHelmState::Contested)
	{
		return false;
	}

	const UNavalVesselComponent* VesselComponent = GetVessel();
	return !VesselComponent || VesselComponent->GetVesselState() != ENavalVesselState::Wreck;
}

ENavalHelmState UNavalHelmComponent::GetHelmState() const
{
	const UNavalPartComponent* Core = GetCorePart();
	if (Core && !Core->IsFunctional())
	{
		return ENavalHelmState::CoreDisabled;
	}

	if (CaptureProgress > 0.0f && NavalTeam::IsValidTeam(CapturingTeamId))
	{
		return ENavalHelmState::Contested;
	}

	if (Core && Core->GetDurabilityFraction() < DamagedCoreFraction)
	{
		return ENavalHelmState::Damaged;
	}

	return ENavalHelmState::Normal;
}

bool UNavalHelmComponent::BeginCapture(AActor* InChallenger)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !InChallenger)
	{
		return false;
	}

	UNavalVesselComponent* VesselComponent = GetVessel();
	const int32 ChallengerTeam = NavalTeam::GetTeamId(InChallenger);
	if (!NavalTeam::IsValidTeam(ChallengerTeam))
	{
		return false;
	}
	if (VesselComponent && ChallengerTeam == VesselComponent->GetTeamId())
	{
		return false;
	}

	const double DistanceSquared = FVector::DistSquared(InChallenger->GetActorLocation(), GetHelmWorldLocation());
	if (DistanceSquared > FMath::Square(static_cast<double>(InteractionRange)))
	{
		return false;
	}

	// A second team taking over an in-progress capture starts from scratch rather than
	// inheriting the first team's work.
	if (NavalTeam::IsValidTeam(CapturingTeamId) && CapturingTeamId != ChallengerTeam)
	{
		CaptureProgress = 0.0f;
		ContestContactSeconds = 0.0f;
	}

	CaptureChallenger = InChallenger;
	CapturingTeamId = ChallengerTeam;
	UpdateTickEnabled();

	FNavalAlertMessage Alert;
	Alert.Vessel = GetOwner();
	Alert.AlertTag = NavalGameplayTags::Alert_CoreContested;
	Alert.WorldLocation = GetHelmWorldLocation();
	Alert.TeamId = VesselComponent ? VesselComponent->GetTeamId() : INDEX_NONE;
	if (const UWorld* World = GetWorld())
	{
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(NavalGameplayTags::Message_Vessel_Alert, Alert);
	}

	BroadcastHelmState();
	return true;
}

void UNavalHelmComponent::EndCapture(AActor* InChallenger)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (InChallenger != nullptr && CaptureChallenger != InChallenger)
	{
		return;
	}

	CaptureChallenger = nullptr;
	ContestContactSeconds = 0.0f;
	UpdateTickEnabled();
	BroadcastHelmState();
}

void UNavalHelmComponent::InterruptCapture(float ProgressPenalty)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || CaptureProgress <= 0.0f)
	{
		return;
	}

	CaptureProgress = FMath::Max(0.0f, CaptureProgress - FMath::Max(0.0f, ProgressPenalty));
	ContestContactSeconds = 0.0f;
	if (CaptureProgress <= 0.0f)
	{
		CapturingTeamId = INDEX_NONE;
		CaptureChallenger = nullptr;
	}
	GetOwner()->ForceNetUpdate();
	UpdateTickEnabled();
	BroadcastHelmState();
}

void UNavalHelmComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	UpdateCapture(DeltaTime);
}

void UNavalHelmComponent::UpdateCapture(float DeltaTime)
{
	const AActor* ActiveChallenger = CaptureChallenger;
	const bool bInRange = ActiveChallenger
		&& FVector::DistSquared(ActiveChallenger->GetActorLocation(), GetHelmWorldLocation())
			<= FMath::Square(static_cast<double>(InteractionRange));

	if (bInRange)
	{
		ContestContactSeconds += DeltaTime;
		if (ContestContactSeconds >= ContestEntrySeconds && CaptureSeconds > 0.0f)
		{
			CaptureProgress = FMath::Clamp(CaptureProgress + DeltaTime / CaptureSeconds, 0.0f, 1.0f);
			if (CaptureProgress >= 1.0f)
			{
				CompleteCapture();
				return;
			}
		}
	}
	else
	{
		// Letting go rolls back slowly. Design 8.6 is explicit that a brief release must not
		// wipe the attempt, but walking away does.
		CaptureChallenger = nullptr;
		ContestContactSeconds = FMath::Max(0.0f, ContestContactSeconds - DeltaTime);
		CaptureProgress = FMath::Max(0.0f, CaptureProgress - CaptureDecayPerSecond * DeltaTime);
		if (CaptureProgress <= 0.0f)
		{
			CapturingTeamId = INDEX_NONE;
			UpdateTickEnabled();
		}
	}

	GetOwner()->ForceNetUpdate();
	BroadcastHelmState();
}

void UNavalHelmComponent::CompleteCapture()
{
	UNavalVesselComponent* VesselComponent = GetVessel();
	const int32 NewTeamId = CapturingTeamId;

	CaptureProgress = 0.0f;
	CapturingTeamId = INDEX_NONE;
	CaptureChallenger = nullptr;
	ContestContactSeconds = 0.0f;
	// Whoever was steering for the old owner is no longer authorised to.
	Operator = nullptr;
	UpdateTickEnabled();

	if (VesselComponent)
	{
		// Ownership transfer is one authoritative write. Windows, doors, storage and heavy
		// weapons all read team from here, so they change hands together or not at all.
		VesselComponent->SetTeamId(NewTeamId);
	}

	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[Helm] Capture complete vessel=%s new_team=%d"),
		*GetNameSafe(GetOwner()),
		NewTeamId);

	GetOwner()->ForceNetUpdate();
	OnHelmChanged.Broadcast(this);
	BroadcastHelmState();
}

void UNavalHelmComponent::UpdateTickEnabled()
{
	SetComponentTickEnabled(CaptureChallenger != nullptr || CaptureProgress > 0.0f);
}

UNavalPartComponent* UNavalHelmComponent::GetCorePart() const
{
	return HelmActor ? HelmActor->GetCorePart() : nullptr;
}

UNavalVesselComponent* UNavalHelmComponent::GetVessel() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UNavalVesselComponent>() : nullptr;
}

FVector UNavalHelmComponent::GetHelmWorldLocation() const
{
	if (HelmActor)
	{
		return HelmActor->GetActorLocation();
	}

	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorTransform().TransformPosition(HelmLocalOffset) : FVector::ZeroVector;
}

void UNavalHelmComponent::BroadcastHelmState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FNavalHelmMessage Message;
	Message.Vessel = GetOwner();
	Message.HelmState = GetHelmState();
	Message.Operator = Operator;
	Message.CaptureProgress = CaptureProgress;
	Message.CapturingTeamId = CapturingTeamId;
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(NavalGameplayTags::Message_Vessel_Helm, Message);
}
