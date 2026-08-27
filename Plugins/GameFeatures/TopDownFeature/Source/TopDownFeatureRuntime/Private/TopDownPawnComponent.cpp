// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownPawnComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/LyraHeroComponent.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "Character/LyraPawnData.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "CommonUIExtensions.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Input/LyraInputConfig.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "TopDownFeatureGameplayTags.h"
#include "TopDownCameraDragInputWidget.h"
#include "TopDownInputWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownPawnComponent)

DEFINE_LOG_CATEGORY_STATIC(LogTopDownPawnComponent, Log, All);

UTopDownPawnComponent::UTopDownPawnComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ClickInputTag(TopDownFeatureGameplayTags::InputTag_TopDownClick)
	, MoveForwardInputTag(TopDownFeatureGameplayTags::InputTag_TopDownMoveForward)
	, MoveBackwardInputTag(TopDownFeatureGameplayTags::InputTag_TopDownMoveBackward)
	, MoveRightInputTag(TopDownFeatureGameplayTags::InputTag_TopDownMoveRight)
	, MoveLeftInputTag(TopDownFeatureGameplayTags::InputTag_TopDownMoveLeft)
	, CameraZoomInputTag(TopDownFeatureGameplayTags::InputTag_TopDownCameraZoom)
	, CameraRotateHoldInputTag(TopDownFeatureGameplayTags::InputTag_TopDownCameraRotateHold)
	, CameraRotateInputTag(TopDownFeatureGameplayTags::InputTag_TopDownCameraRotate)
	, InputWidgetClass(UTopDownInputWidget::StaticClass())
	, CameraDragInputWidgetClass(UTopDownCameraDragInputWidget::StaticClass())
	, UILayerTag(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Game"), false))
	, GroundTraceChannel(ECC_Visibility)
	, MaxGroundTraceDistance(100000.0f)
	, bTraceComplex(false)
	, FacingRotationInterpSpeed(0.0f)
	, AcceptanceRadius(75.0f)
	, InitialCameraDistance(1800.0f)
	, MinCameraDistance(600.0f)
	, MaxCameraDistance(3200.0f)
	, ZoomUnitsPerStep(180.0f)
	, RotationDegreesPerPixel(0.25f)
	, BoundInputComponent(nullptr)
	, PushedInputWidget(nullptr)
	, PushedCameraDragInputWidget(nullptr)
	, MoveTarget(FVector::ZeroVector)
	, CameraDistance(InitialCameraDistance)
	, CameraYawOffset(0.0f)
	, bHasMoveTarget(false)
	, bInputBound(false)
	, bCameraRotateHeld(false)
	, bOriginalUseControllerRotationYaw(true)
	, bRotationPolicyOverridden(false)
{
	FacingBlockedTags.AddTag(TAG_Gameplay_MovementStopped);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UTopDownPawnComponent::BeginPlay()
{
	Super::BeginPlay();
	CameraDistance = FMath::Clamp(InitialCameraDistance, MinCameraDistance, FMath::Max(MinCameraDistance, MaxCameraDistance));
	CameraYawOffset = 0.0f;

	APawn* Pawn = GetPawn<APawn>();
	if (!ensure(Pawn))
	{
		return;
	}

	// Lyra characters normally use the controller yaw for FPS-style aiming. In this
	// feature the controller rotation remains the camera/movement frame, while the
	// pawn's actor yaw follows the mouse-plane direction instead.
	bOriginalUseControllerRotationYaw = Pawn->bUseControllerRotationYaw;
	Pawn->bUseControllerRotationYaw = false;
	bRotationPolicyOverridden = true;

	if (UGameFrameworkComponentManager* ComponentManager = UGameFrameworkComponentManager::GetForActor(Pawn))
	{
		const UGameFrameworkComponentManager::FExtensionHandlerDelegate ExtensionDelegate =
			UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandlePawnExtension);
		ExtensionRequestHandle = ComponentManager->AddExtensionHandler(Pawn->GetClass(), ExtensionDelegate);
	}

	// The component can be injected after Hero initialization and miss the original extension event.
	BindInputIfReady();
}

void UTopDownPawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExtensionRequestHandle.Reset();
	UnbindInput();
	CancelMoveToTarget();

	if (bRotationPolicyOverridden)
	{
		if (APawn* Pawn = GetPawn<APawn>())
		{
			Pawn->bUseControllerRotationYaw = bOriginalUseControllerRotationYaw;
		}
		bRotationPolicyOverridden = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UTopDownPawnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		CancelMoveToTarget();
		return;
	}

	UpdateFacingFromMouse(DeltaTime);

	// Keep the old programmatic target API functional for existing Blueprint callers,
	// but no input action invokes it anymore.
	if (!bHasMoveTarget)
	{
		return;
	}

	FVector Direction = MoveTarget - Pawn->GetActorLocation();
	Direction.Z = 0.0f;

	if (Direction.SizeSquared2D() <= FMath::Square(AcceptanceRadius))
	{
		CancelMoveToTarget();
		return;
	}

	Pawn->AddMovementInput(Direction.GetSafeNormal2D());
}

bool UTopDownPawnComponent::SetMoveTargetUnderCursor()
{
	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (!PlayerController || !PlayerController->IsLocalController() || !World)
	{
		return false;
	}

	FVector TraceStart;
	FVector TraceDirection;
	if (!PlayerController->DeprojectMousePositionToWorld(TraceStart, TraceDirection))
	{
		return false;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TopDownMoveTarget), bTraceComplex, Pawn);
	const FVector TraceEnd = TraceStart + TraceDirection * MaxGroundTraceDistance;
	if (!World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
	{
		return false;
	}

	SetMoveTarget(HitResult.ImpactPoint);
	return true;
}

void UTopDownPawnComponent::SetMoveTarget(const FVector& TargetLocation)
{
	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}

	MoveTarget = TargetLocation;
	bHasMoveTarget = true;
	SetComponentTickEnabled(true);
}

void UTopDownPawnComponent::CancelMoveToTarget()
{
	bHasMoveTarget = false;
	// The component tick also updates mouse-facing yaw. Keep it alive while the
	// feature is bound; only tear it down when input has been unbound.
	if (!bInputBound)
	{
		SetComponentTickEnabled(false);
	}
}

void UTopDownPawnComponent::HandlePawnExtension(AActor* Actor, FName EventName)
{
	if (Actor != GetOwner())
	{
		return;
	}

	if ((EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved) ||
		(EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved))
	{
		UnbindInput();
		CancelMoveToTarget();
	}
	else if ((EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded) ||
		(EventName == ULyraHeroComponent::NAME_BindInputsNow))
	{
		BindInputIfReady();
	}
}

void UTopDownPawnComponent::BindInputIfReady()
{
	if (bInputBound)
	{
		return;
	}

	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	const ULyraHeroComponent* HeroComponent = ULyraHeroComponent::FindHeroComponent(Pawn);
	if (!HeroComponent || !HeroComponent->IsReadyToBindInputs())
	{
		return;
	}
	EnsureInputWidget(PlayerController);

	const ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	const ULyraPawnData* PawnData = PawnExtension ? PawnExtension->GetPawnData<ULyraPawnData>() : nullptr;
	const ULyraInputConfig* InputConfig = PawnData ? PawnData->InputConfig : nullptr;
	UEnhancedInputComponent* InputComponent = Pawn->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputConfig || !InputComponent || !MoveForwardInputTag.IsValid() || !MoveBackwardInputTag.IsValid() ||
		!MoveRightInputTag.IsValid() || !MoveLeftInputTag.IsValid() || !CameraZoomInputTag.IsValid() ||
		!CameraRotateHoldInputTag.IsValid() || !CameraRotateInputTag.IsValid())
	{
		return;
	}

	// ULyraInputConfig's lookup helper is not exported from LyraGame, so GameFeature
	// modules must inspect the public native-action entries instead of calling it.
	const auto FindNativeAction = [InputConfig](const FGameplayTag& InputTag) -> const UInputAction*
	{
		for (const FLyraInputAction& NativeAction : InputConfig->NativeInputActions)
		{
			if (NativeAction.InputAction && NativeAction.InputTag == InputTag)
			{
				return NativeAction.InputAction;
			}
		}
		return nullptr;
	};

	const UInputAction* MoveForwardAction = FindNativeAction(MoveForwardInputTag);
	const UInputAction* MoveBackwardAction = FindNativeAction(MoveBackwardInputTag);
	const UInputAction* MoveRightAction = FindNativeAction(MoveRightInputTag);
	const UInputAction* MoveLeftAction = FindNativeAction(MoveLeftInputTag);
	const UInputAction* ZoomAction = FindNativeAction(CameraZoomInputTag);
	const UInputAction* RotateHoldAction = FindNativeAction(CameraRotateHoldInputTag);
	const UInputAction* RotateAction = FindNativeAction(CameraRotateInputTag);
	if (!MoveForwardAction || !MoveBackwardAction || !MoveRightAction || !MoveLeftAction ||
		!ZoomAction || !RotateHoldAction || !RotateAction)
	{
		UE_LOG(LogTopDownPawnComponent, Warning,
			TEXT("PawnData InputConfig '%s' is missing one or more top-down native actions; top-down input is disabled for '%s'."),
			*GetNameSafe(InputConfig), *GetNameSafe(Pawn));
		return;
	}

	InputBindingHandles.Add(
		InputComponent->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &ThisClass::Input_MoveForward).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(MoveBackwardAction, ETriggerEvent::Triggered, this, &ThisClass::Input_MoveBackward).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ThisClass::Input_MoveRight).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(MoveLeftAction, ETriggerEvent::Triggered, this, &ThisClass::Input_MoveLeft).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ThisClass::Input_CameraZoom).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(RotateHoldAction, ETriggerEvent::Started, this, &ThisClass::Input_CameraRotateStarted).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(RotateHoldAction, ETriggerEvent::Completed, this, &ThisClass::Input_CameraRotateCompleted).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(RotateHoldAction, ETriggerEvent::Canceled, this, &ThisClass::Input_CameraRotateCompleted).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(RotateAction, ETriggerEvent::Triggered, this, &ThisClass::Input_CameraRotate).GetHandle());
	BoundInputComponent = InputComponent;
	bInputBound = true;
	SetComponentTickEnabled(true);

}

void UTopDownPawnComponent::UnbindInput()
{
	if (BoundInputComponent)
	{
		for (const uint32 Handle : InputBindingHandles)
		{
			BoundInputComponent->RemoveBindingByHandle(Handle);
		}
	}
	InputBindingHandles.Reset();
	bCameraRotateHeld = false;
	PopCameraDragInputWidget();

	RemoveInputWidget();

	BoundInputComponent = nullptr;
	bInputBound = false;
}

void UTopDownPawnComponent::EnsureInputWidget(APlayerController* PlayerController)
{
	if (PushedInputWidget || !PlayerController || !InputWidgetClass || !UILayerTag.IsValid())
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		PushedInputWidget = UCommonUIExtensions::PushContentToLayer_ForPlayer(
			LocalPlayer,
			UILayerTag,
			InputWidgetClass);
	}
}

void UTopDownPawnComponent::RemoveInputWidget()
{
	if (PushedInputWidget)
	{
		UCommonUIExtensions::PopContentFromLayer(PushedInputWidget);
		PushedInputWidget = nullptr;
	}
}

void UTopDownPawnComponent::PushCameraDragInputWidget(APlayerController* PlayerController)
{
	if (PushedCameraDragInputWidget || !PlayerController || !CameraDragInputWidgetClass || !UILayerTag.IsValid())
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		PushedCameraDragInputWidget = UCommonUIExtensions::PushContentToLayer_ForPlayer(
			LocalPlayer, UILayerTag, CameraDragInputWidgetClass);
	}
}

void UTopDownPawnComponent::PopCameraDragInputWidget()
{
	if (PushedCameraDragInputWidget)
	{
		UCommonUIExtensions::PopContentFromLayer(PushedCameraDragInputWidget);
		PushedCameraDragInputWidget = nullptr;
	}
}

void UTopDownPawnComponent::Input_MoveForward(const FInputActionValue& InputActionValue)
{
	CancelMoveToTarget();
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Pawn || !Controller)
	{
		return;
	}

	const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::ForwardVector), InputActionValue.Get<float>());
}

void UTopDownPawnComponent::Input_MoveBackward(const FInputActionValue& InputActionValue)
{
	CancelMoveToTarget();
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Pawn || !Controller)
	{
		return;
	}

	const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::ForwardVector), -InputActionValue.Get<float>());
}

void UTopDownPawnComponent::Input_MoveRight(const FInputActionValue& InputActionValue)
{
	CancelMoveToTarget();
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Pawn || !Controller)
	{
		return;
	}

	const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::RightVector), InputActionValue.Get<float>());
}

void UTopDownPawnComponent::Input_MoveLeft(const FInputActionValue& InputActionValue)
{
	CancelMoveToTarget();
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Pawn || !Controller)
	{
		return;
	}

	const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::RightVector), -InputActionValue.Get<float>());
}

void UTopDownPawnComponent::Input_CameraZoom(const FInputActionValue& InputActionValue)
{
	CameraDistance = FMath::Clamp(
		CameraDistance - InputActionValue.Get<float>() * ZoomUnitsPerStep,
		MinCameraDistance,
		FMath::Max(MinCameraDistance, MaxCameraDistance));
}

void UTopDownPawnComponent::Input_CameraRotateStarted(const FInputActionValue& InputActionValue)
{
	bCameraRotateHeld = true;
	APawn* Pawn = GetPawn<APawn>();
	PushCameraDragInputWidget(Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr);
}

void UTopDownPawnComponent::Input_CameraRotateCompleted(const FInputActionValue& InputActionValue)
{
	bCameraRotateHeld = false;
	PopCameraDragInputWidget();
}

void UTopDownPawnComponent::Input_CameraRotate(const FInputActionValue& InputActionValue)
{
	if (!bCameraRotateHeld)
	{
		return;
	}

	const FVector2D PointerDelta = InputActionValue.Get<FVector2D>();
	CameraYawOffset = FRotator::NormalizeAxis(CameraYawOffset + PointerDelta.X * RotationDegreesPerPixel);
}

void UTopDownPawnComponent::UpdateFacingFromMouse(float DeltaTime)
{
	if (bCameraRotateHeld)
	{
		return;
	}

	// The ASC is the explicit owner of station/building control state.  Attachment is only a
	// presentation detail and can be transient while prediction or replication catches up.
	if (const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		if (AbilitySystem->HasAnyMatchingGameplayTags(FacingBlockedTags))
		{
			return;
		}
	}

	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return;
	}

	RayDirection = RayDirection.GetSafeNormal();
	if (FMath::IsNearlyZero(RayDirection.Z))
	{
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	const float RayDistance = (PawnLocation.Z - RayOrigin.Z) / RayDirection.Z;
	if (RayDistance <= 0.0f)
	{
		return;
	}

	FVector FacingDirection = RayOrigin + RayDirection * RayDistance - PawnLocation;
	FacingDirection.Z = 0.0f;
	if (FacingDirection.SizeSquared2D() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float DesiredYaw = FacingDirection.Rotation().Yaw;
	const float CurrentYaw = Pawn->GetActorRotation().Yaw;
	const float MaxYawStep = FacingRotationInterpSpeed > 0.0f
		? FacingRotationInterpSpeed * DeltaTime
		: 360.0f;
	const float NewYaw = FMath::FixedTurn(CurrentYaw, DesiredYaw, MaxYawStep);
	Pawn->SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
}
