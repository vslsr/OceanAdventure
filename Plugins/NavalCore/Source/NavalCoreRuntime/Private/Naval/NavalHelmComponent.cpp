// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHelmComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHelmActor.h"
#include "Naval/NavalHelmStation.h"
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
}

void UNavalHelmComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
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

	ActiveStation = nullptr;
	Operator = nullptr;
	CaptureChallenger = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UNavalHelmComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalHelmComponent, Operator);
	DOREPLIFETIME(UNavalHelmComponent, ActiveStation);
	DOREPLIFETIME(UNavalHelmComponent, CaptureProgress);
	DOREPLIFETIME(UNavalHelmComponent, CapturingTeamId);
	DOREPLIFETIME(UNavalHelmComponent, ThrottleIntent);
	DOREPLIFETIME(UNavalHelmComponent, SteerIntent);
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

	// Reach and station damage belong to the concrete INavalHelmStation. This component owns
	// only vessel-wide team, wreck and occupancy truth.
	return true;
}

bool UNavalHelmComponent::TryOccupy(AActor* NewOperator)
{
	return TryOccupyFromStation(NewOperator, GetOwner());
}

bool UNavalHelmComponent::TryOccupyFromStation(AActor* NewOperator, AActor* StationActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	const INavalHelmStation* Station = StationActor ? Cast<INavalHelmStation>(StationActor) : nullptr;
	if (!Station || Station->GetHelmComponent() != this
		|| !Station->CanOperate(NewOperator, FailReason))
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
	ActiveStation = StationActor;
	OperatorLostControllerTime = 0.0;
	BindOperatorDestroyed(NewOperator);
	ResetControlIntent();
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
	if (CaptureChallenger == nullptr && CaptureProgress <= 0.0f)
	{
		ActiveStation = nullptr;
	}
	OperatorLostControllerTime = 0.0;
	// Design 8.3.1: the ship keeps its heading and coasts down. No autopilot, no snap to zero.
	ResetControlIntent();
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
		UE_LOG(LogNavalCore, Verbose,
			TEXT("[NavalInputTrace] phase=helm-intent result=not-authority vessel=%s source=%s throttle=%.3f steer=%.3f"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Source), InThrottle, InSteer);
		return;
	}

	// The client sends this as a request every frame it holds a direction; only the actor the
	// server believes is at the wheel can move the ship.
	const bool bAcceptsControlInput = AcceptsControlInput();
	if (Source == nullptr || Operator != Source || !bAcceptsControlInput)
	{
		UE_LOG(LogNavalCore, Verbose,
			TEXT("[NavalInputTrace] phase=helm-intent result=rejected vessel=%s source=%s operator=%s active_station=%s throttle=%.3f steer=%.3f source_valid=%d operator_matches=%d accepts_input=%d helm_state=%d"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Source), *GetNameSafe(Operator.Get()),
			*GetNameSafe(ActiveStation.Get()), InThrottle, InSteer, Source != nullptr,
			Operator == Source, bAcceptsControlInput, static_cast<int32>(GetHelmState()));
		return;
	}

	ThrottleIntent = FMath::Clamp(InThrottle, -1.0f, 1.0f);
	SteerIntent = FMath::Clamp(InSteer, -1.0f, 1.0f);
	WorldMoveIntent = FVector2D::ZeroVector;
	FacingTarget = FVector::ZeroVector;
	bHasFacingTarget = false;
	UE_LOG(LogNavalCore, Verbose,
		TEXT("[NavalInputTrace] phase=helm-intent result=accepted vessel=%s source=%s active_station=%s throttle=%.3f steer=%.3f helm_state=%d"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Source), *GetNameSafe(ActiveStation.Get()),
		ThrottleIntent, SteerIntent, static_cast<int32>(GetHelmState()));
}

void UNavalHelmComponent::SetDirectControlIntent(
	AActor* Source,
	FVector2D InWorldMoveIntent,
	FVector InFacingTarget,
	bool bInHasFacingTarget)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (Source == nullptr || Operator != Source || !AcceptsControlInput())
	{
		UE_LOG(
			LogNavalCore,
			Verbose,
			TEXT("[NavalInputTrace] phase=direct-intent result=rejected vessel=%s source=%s operator=%s"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Source),
			*GetNameSafe(Operator.Get()));
		return;
	}

	if (!FMath::IsFinite(InWorldMoveIntent.X) || !FMath::IsFinite(InWorldMoveIntent.Y))
	{
		InWorldMoveIntent = FVector2D::ZeroVector;
	}
	WorldMoveIntent = InWorldMoveIntent.GetClampedToMaxSize(1.0f);
	ThrottleIntent = 0.0f;
	SteerIntent = 0.0f;

	bHasFacingTarget = bInHasFacingTarget && !InFacingTarget.ContainsNaN();
	FacingTarget = bHasFacingTarget ? InFacingTarget : FVector::ZeroVector;
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
	return BeginCaptureAtStation(InChallenger, ActiveStation ? ActiveStation.Get() : GetOwner());
}

FVector2D UNavalHelmComponent::GetWorldMoveIntent() const
{
	return AcceptsControlInput() ? WorldMoveIntent : FVector2D::ZeroVector;
}

void UNavalHelmComponent::ResetControlIntent()
{
	ThrottleIntent = 0.0f;
	SteerIntent = 0.0f;
	WorldMoveIntent = FVector2D::ZeroVector;
	FacingTarget = FVector::ZeroVector;
	bHasFacingTarget = false;
}

bool UNavalHelmComponent::BeginCaptureAtStation(AActor* InChallenger, AActor* StationActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !InChallenger)
	{
		return false;
	}

	const INavalHelmStation* Station = StationActor ? Cast<INavalHelmStation>(StationActor) : nullptr;
	if (!Station || Station->GetHelmComponent() != this || !Station->IsWithinInteractionRange(InChallenger))
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

	// A second team taking over an in-progress capture starts from scratch rather than
	// inheriting the first team's work.
	if (NavalTeam::IsValidTeam(CapturingTeamId) && CapturingTeamId != ChallengerTeam)
	{
		CaptureProgress = 0.0f;
		ContestContactSeconds = 0.0f;
	}

	CaptureChallenger = InChallenger;
	ActiveStation = StationActor;
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
		if (Operator == nullptr)
		{
			ActiveStation = nullptr;
		}
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
	const INavalHelmStation* Station = ActiveStation ? Cast<INavalHelmStation>(ActiveStation) : nullptr;
	const bool bInRange = Station && Station->IsWithinInteractionRange(ActiveChallenger);

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
			if (Operator == nullptr)
			{
				ActiveStation = nullptr;
			}
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
	UnbindOperatorDestroyed();
	Operator = nullptr;
	ActiveStation = nullptr;
	ResetControlIntent();
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
	const INavalHelmStation* Station = ActiveStation ? Cast<INavalHelmStation>(ActiveStation) : nullptr;
	return Station ? Station->GetHelmCorePart() : nullptr;
}

ANavalHelmActor* UNavalHelmComponent::GetHelmActor() const
{
	return Cast<ANavalHelmActor>(ActiveStation);
}

UNavalVesselComponent* UNavalHelmComponent::GetVessel() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UNavalVesselComponent>() : nullptr;
}

FVector UNavalHelmComponent::GetHelmWorldLocation() const
{
	if (const INavalHelmStation* Station = ActiveStation ? Cast<INavalHelmStation>(ActiveStation) : nullptr)
	{
		return Station->GetInteractionLocation();
	}

	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
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
