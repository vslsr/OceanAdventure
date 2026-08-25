// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_FireHeavyWeapon.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalBallistics.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/OceanAdventureAbilityTask_NavalControl.h"
#include "Naval/OceanAdventureCannonTrajectoryPreview.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_FireHeavyWeapon)

UOceanAdventureGameplayAbility_FireHeavyWeapon::UOceanAdventureGameplayAbility_FireHeavyWeapon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	// The input edge activates one instance on the player's ASC.  Holding that instance
	// only updates charge; the release task submits exactly one TargetData request.
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
}

bool UOceanAdventureGameplayAbility_FireHeavyWeapon::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		UE_LOG(LogOceanAdventure, Display,
			TEXT("[NavalFire] CanActivate rejected by Super avatar=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr));
		return false;
	}

	// Firing only exists while the player is actually at a gun, on both client and server.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const bool bOperating = AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon);
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const ANavalHeavyWeaponActor* Weapon = FindOperatedWeapon(Handle, ActorInfo);
	const bool bAtGrantedWeapon = Avatar && Weapon && Avatar->GetAttachParentActor() == Weapon;
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalFire] CanActivate avatar=%s weapon=%s has_asc=%d operating_tag=%d at_source_weapon=%d"),
		*GetNameSafe(Avatar), *GetNameSafe(Weapon), AbilitySystem != nullptr, bOperating, bAtGrantedWeapon);
	return bOperating && bAtGrantedWeapon;
}

ANavalHeavyWeaponActor* UOceanAdventureGameplayAbility_FireHeavyWeapon::FindOperatedWeapon(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	ULyraAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<ULyraAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystem
		? AbilitySystem->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	return Spec ? Cast<ANavalHeavyWeaponActor>(Spec->SourceObject.Get()) : nullptr;
}

ANavalHeavyWeaponActor* UOceanAdventureGameplayAbility_FireHeavyWeapon::FindOperatedWeapon() const
{
	return FindOperatedWeapon(CurrentSpecHandle, CurrentActorInfo);
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bFireResolved = false;
	ChargeElapsedSeconds = 0.0f;
	CurrentChargeAlpha = MinimumChargeAlpha;
	ChargingWeapon.Reset();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalFire] Activate avatar=%s local=%d authority=%d attached_to=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), ActorInfo && ActorInfo->IsLocallyControlled(),
		HasAuthority(&ActivationInfo),
		*GetNameSafe(GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetAttachParentActor() : nullptr));

	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	OnTargetDataReadyHandle = AbilitySystem
		->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		.AddUObject(this, &ThisClass::OnTargetDataReadyCallback);

	if (!ActorInfo->IsLocallyControlled())
	{
		AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
			Handle, ActivationInfo.GetActivationPredictionKey());
		return;
	}

	ANavalHeavyWeaponActor* Weapon = FindOperatedWeapon();
	if (!Weapon)
	{
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalFire] No operated weapon found avatar=%s attached_to=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GetNameSafe(GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetAttachParentActor() : nullptr));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ChargingWeapon = Weapon;
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetAvatarActorFromActorInfo();
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TrajectoryPreview = World->SpawnActor<AOceanAdventureCannonTrajectoryPreview>(
			AOceanAdventureCannonTrajectoryPreview::StaticClass(), FTransform::Identity, SpawnParameters);
	}

	ChargeTask = UOceanAdventureAbilityTask_NavalControl::NavalControlTick(this, PreviewSampleInterval);
	if (ChargeTask)
	{
		ChargeTask->OnControlSample.AddUObject(this, &ThisClass::UpdateCharge);
		ChargeTask->ReadyForActivation();
	}

	// Test the already-released state: activation lands a frame after the press edge, so a
	// quick click can release the button before this task subscribes. Waiting only for a
	// future event would drop that release, leave the instance charging, and make the next
	// click's release fire the previous shot -- the gun would only answer to a double click.
	UAbilityTask_WaitInputRelease* ReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(
		this, /*bTestAlreadyReleased=*/true);
	if (ReleaseTask)
	{
		ReleaseTask->OnRelease.AddDynamic(this, &ThisClass::OnInputReleased);
		ReleaseTask->ReadyForActivation();
	}
	UpdateTrajectoryPreview();
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::UpdateCharge(float DeltaTime)
{
	ChargeElapsedSeconds += DeltaTime;
	CurrentChargeAlpha = FMath::Clamp(
		ChargeElapsedSeconds / FMath::Max(0.1f, MaxChargeSeconds),
		MinimumChargeAlpha,
		1.0f);
	UpdateTrajectoryPreview();
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::UpdateTrajectoryPreview()
{
	ANavalHeavyWeaponActor* Weapon = ChargingWeapon.Get();
	UWorld* World = GetWorld();
	if (!Weapon || !TrajectoryPreview || !World)
	{
		return;
	}

	const FVector Muzzle = Weapon->GetMuzzleLocation();
	FVector AimDirection = Weapon->GetMuzzleDirection().GetSafeNormal2D();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection = Weapon->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector RequestedAim = Muzzle + AimDirection * Weapon->GetMaxRange();
	FVector InitialVelocity = FVector::ZeroVector;
	float GravityZ = 0.0f;
	float Range = 0.0f;
	Weapon->BuildChargedTrajectory(RequestedAim, CurrentChargeAlpha, InitialVelocity, GravityZ, Range);

	const float PlanarSpeed = InitialVelocity.Size2D();
	float MaxTime = PlanarSpeed > UE_KINDA_SMALL_NUMBER ? Range / PlanarSpeed : 0.0f;
	bool bBlocked = false;

	// A wall blocks the horizontal fire line even if the visual parabola could rise above it.
	FNavalShotQuery HorizontalQuery;
	HorizontalQuery.WorldContextObject = this;
	HorizontalQuery.Start = Muzzle;
	HorizontalQuery.End = Muzzle + AimDirection * Range;
	HorizontalQuery.TeamId = Weapon->GetTeamId();
	HorizontalQuery.IgnoreActors.Add(Weapon);
	if (AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		HorizontalQuery.IgnoreActors.Add(Avatar);
	}
	ENavalShotBlockReason HorizontalReason = ENavalShotBlockReason::None;
	FVector HorizontalBlockLocation = HorizontalQuery.End;
	if (!FNavalBallistics::IsFireLineClear(HorizontalQuery, HorizontalReason, HorizontalBlockLocation))
	{
		MaxTime = FMath::Min(MaxTime, FVector::Dist2D(Muzzle, HorizontalBlockLocation) / FMath::Max(1.0f, PlanarSpeed));
		bBlocked = true;
	}

	TArray<FVector> Points;
	Points.Reserve(FMath::CeilToInt(MaxTime / PreviewSampleInterval) + 2);
	Points.Add(Muzzle);
	FVector Previous = Muzzle;
	const float SampleStep = FMath::Max(0.016f, PreviewSampleInterval);
	for (float Time = FMath::Min(SampleStep, MaxTime); MaxTime > 0.0f; Time = FMath::Min(Time + SampleStep, MaxTime))
	{
		const float ClampedTime = FMath::Min(Time, MaxTime);
		const FVector Point = Muzzle + InitialVelocity * ClampedTime
			+ FVector(0.0f, 0.0f, 0.5f * GravityZ * FMath::Square(ClampedTime));

		FNavalShotQuery SegmentQuery = HorizontalQuery;
		SegmentQuery.Start = Previous;
		SegmentQuery.End = Point;
		FNavalShotResult SegmentResult;
		if (FNavalBallistics::ResolveShot(SegmentQuery, SegmentResult) && SegmentResult.bHit)
		{
			Points.Add(SegmentResult.Hit.ImpactPoint);
			bBlocked = bBlocked || FNavalBallistics::IsStructuralBlocker(SegmentResult.Hit);
			break;
		}

		Points.Add(Point);
		Previous = Point;
		if (FMath::IsNearlyEqual(ClampedTime, MaxTime))
		{
			break;
		}
	}

	TrajectoryPreview->SetTrajectory(Points, bBlocked);
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::OnInputReleased(float /*TimeHeld*/)
{
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalFire] Input released avatar=%s charge=%.2f elapsed=%.2f"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), CurrentChargeAlpha, ChargeElapsedSeconds);
	CommitChargedShot();
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::CommitChargedShot()
{
	ANavalHeavyWeaponActor* Weapon = ChargingWeapon.Get();
	if (!Weapon || bFireResolved)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const FVector Muzzle = Weapon->GetMuzzleLocation();
	const FVector AimLocation = Muzzle + Weapon->GetMuzzleDirection().GetSafeNormal2D()
		* FMath::Lerp(Weapon->GetMinimumRange(), Weapon->GetMaxRange(), CurrentChargeAlpha);
	FGameplayTag FailReason;
	if (!Weapon->CanFire(GetAvatarActorFromActorInfo(), AimLocation, FailReason))
	{
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalFire] Local CanFire refused weapon=%s avatar=%s reason=%s aim=%s charge=%.2f"),
			*GetNameSafe(Weapon), *GetNameSafe(GetAvatarActorFromActorInfo()), *FailReason.ToString(),
			*AimLocation.ToString(), CurrentChargeAlpha);
		BroadcastFailure(FailReason, Weapon);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	FOceanAdventureNavalTargetData FireData;
	FireData.StationActor = Weapon;
	FireData.Request = ENavalStationRequest::Fire;
	FireData.AimLocation = AimLocation;
	FireData.SetChargeAlpha(CurrentChargeAlpha);
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalFire] Submit shot weapon=%s avatar=%s aim=%s charge=%.2f"),
		*GetNameSafe(Weapon), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*AimLocation.ToString(), CurrentChargeAlpha);

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(FireData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	if (bFireResolved)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
		return;
	}
	bFireResolved = true;

	FScopedPredictionWindow ScopedPrediction(
		AbilitySystem, CurrentActivationInfo.GetActivationPredictionKey());

	FGameplayAbilityTargetDataHandle LocalData(
		MoveTemp(const_cast<FGameplayAbilityTargetDataHandle&>(InData)));

	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		AbilitySystem->CallServerSetReplicatedTargetData(
			CurrentSpecHandle,
			CurrentActivationInfo.GetActivationPredictionKey(),
			LocalData,
			ApplicationTag,
			AbilitySystem->ScopedPredictionKey);
	}

	if (HasAuthority(&CurrentActivationInfo) && LocalData.Num() > 0)
	{
		const FOceanAdventureNavalTargetData* Data =
			static_cast<const FOceanAdventureNavalTargetData*>(LocalData.Get(0));
		ANavalHeavyWeaponActor* Weapon = Data
			? Cast<ANavalHeavyWeaponActor>(Data->StationActor.Get())
			: nullptr;
		ANavalHeavyWeaponActor* GrantedWeapon = FindOperatedWeapon();

		// The client cannot substitute another station Actor into TargetData. TryFire then
		// re-runs the full operator/range/reload/fire-line validation on that granted weapon.
		if (!Weapon || Weapon != GrantedWeapon || !Weapon->TryFire(
			GetAvatarActorFromActorInfo(), Data->AimLocation, Data->GetChargeAlpha()))
		{
			UE_LOG(
				LogOceanAdventure,
				Verbose,
				TEXT("[NavalFire] Server refused requested_weapon=%s granted_weapon=%s avatar=%s"),
				*GetNameSafe(Weapon),
				*GetNameSafe(GrantedWeapon),
				*GetNameSafe(GetAvatarActorFromActorInfo()));
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ChargeTask)
	{
		ChargeTask->OnControlSample.RemoveAll(this);
		ChargeTask->EndTask();
		ChargeTask = nullptr;
	}
	DestroyTrajectoryPreview();
	ChargingWeapon.Reset();

	if (OnTargetDataReadyHandle.IsValid() && ActorInfo)
	{
		if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
		{
			AbilitySystem->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				.Remove(OnTargetDataReadyHandle);
		}
		OnTargetDataReadyHandle.Reset();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::DestroyTrajectoryPreview()
{
	if (TrajectoryPreview)
	{
		TrajectoryPreview->Destroy();
		TrajectoryPreview = nullptr;
	}
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::BroadcastFailure(
	FGameplayTag FailReason, AActor* Station) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalStationFailedMessage Message;
	Message.FailReason = FailReason.IsValid() ? FailReason : NavalGameplayTags::Fail_NotOperational;
	Message.StationActor = Station;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_StationFailed, Message);
}
