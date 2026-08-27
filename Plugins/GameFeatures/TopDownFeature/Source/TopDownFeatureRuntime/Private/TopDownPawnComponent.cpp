// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownPawnComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/LyraHeroComponent.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "CommonUIExtensions.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Input/LyraInputConfig.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "TopDownFeatureGameplayTags.h"
#include "TopDownCameraDragInputWidget.h"
#include "TopDownInputWidget.h"
#include "UObject/SoftObjectPath.h"

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
	, TopDownInputConfig(FSoftObjectPath(TEXT("/TopDownFeature/Input/DA_TopDown_InputConfig")))
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
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
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
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		// Execute Ability intent before CharacterMovement consumes it in the same frame.
		Movement->AddTickPrerequisiteComponent(this);
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
	ActiveAbilityMoveInputs.Reset();
	CancelMoveToTarget();

	if (bRotationPolicyOverridden)
	{
		if (APawn* Pawn = GetPawn<APawn>())
		{
			Pawn->bUseControllerRotationYaw = bOriginalUseControllerRotationYaw;
		}
		bRotationPolicyOverridden = false;
	}
	if (APawn* Pawn = GetPawn<APawn>())
	{
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->RemoveTickPrerequisiteComponent(this);
		}
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

	const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (AbilitySystem && AbilitySystem->HasAnyMatchingGameplayTags(FacingBlockedTags))
	{
		CancelMoveToTarget();
		return;
	}

	FVector2D AbilityMove = FVector2D::ZeroVector;
	AbilityMove.X += ActiveAbilityMoveInputs.HasTagExact(MoveForwardInputTag) ? 1.0f : 0.0f;
	AbilityMove.X -= ActiveAbilityMoveInputs.HasTagExact(MoveBackwardInputTag) ? 1.0f : 0.0f;
	AbilityMove.Y += ActiveAbilityMoveInputs.HasTagExact(MoveRightInputTag) ? 1.0f : 0.0f;
	AbilityMove.Y -= ActiveAbilityMoveInputs.HasTagExact(MoveLeftInputTag) ? 1.0f : 0.0f;
	if (!AbilityMove.IsNearlyZero())
	{
		CancelMoveToTarget();
		AbilityMove.Normalize();
		if (AController* Controller = Pawn->GetController())
		{
			const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
			Pawn->AddMovementInput(
				MovementRotation.RotateVector(FVector::ForwardVector), AbilityMove.X);
			Pawn->AddMovementInput(
				MovementRotation.RotateVector(FVector::RightVector), AbilityMove.Y);
		}
		return;
	}

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

void UTopDownPawnComponent::SetAbilityMoveInput(FGameplayTag InputTag, bool bPressed)
{
	const bool bIsOwnedDirection = InputTag == MoveForwardInputTag
		|| InputTag == MoveBackwardInputTag
		|| InputTag == MoveRightInputTag
		|| InputTag == MoveLeftInputTag;
	if (!bIsOwnedDirection)
	{
		return;
	}

	if (bPressed)
	{
		ActiveAbilityMoveInputs.AddTag(InputTag);
		CancelMoveToTarget();
		SetComponentTickEnabled(true);
	}
	else
	{
		ActiveAbilityMoveInputs.RemoveTag(InputTag);
	}
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
		ActiveAbilityMoveInputs.Reset();
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

	const ULyraInputConfig* InputConfig = TopDownInputConfig.LoadSynchronous();
	UEnhancedInputComponent* InputComponent = Pawn->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputConfig || !InputComponent || !CameraZoomInputTag.IsValid() ||
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

	const UInputAction* ZoomAction = FindNativeAction(CameraZoomInputTag);
	const UInputAction* RotateHoldAction = FindNativeAction(CameraRotateHoldInputTag);
	const UInputAction* RotateAction = FindNativeAction(CameraRotateInputTag);
	if (!ZoomAction || !RotateHoldAction || !RotateAction)
	{
		UE_LOG(LogTopDownPawnComponent, Warning,
			TEXT("Feature InputConfig '%s' is missing one or more top-down camera actions; top-down input is disabled for '%s'."),
			*GetNameSafe(InputConfig), *GetNameSafe(Pawn));
		return;
	}

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
