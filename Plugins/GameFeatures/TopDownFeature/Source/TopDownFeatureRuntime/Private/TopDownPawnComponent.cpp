// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownPawnComponent.h"

#include "Character/LyraHeroComponent.h"
#include "Character/LyraPawnData.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Input/LyraInputConfig.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "TopDownFeatureGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownPawnComponent)

DEFINE_LOG_CATEGORY_STATIC(LogTopDownPawnComponent, Log, All);

UTopDownPawnComponent::UTopDownPawnComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ClickInputTag(TopDownFeatureGameplayTags::InputTag_TopDownClick)
	, bShowMouseCursor(true)
	, GroundTraceChannel(ECC_Visibility)
	, MaxGroundTraceDistance(100000.0f)
	, bTraceComplex(false)
	, AcceptanceRadius(75.0f)
	, BoundInputComponent(nullptr)
	, CursorController(nullptr)
	, MoveTarget(FVector::ZeroVector)
	, bHasMoveTarget(false)
	, bInputBound(false)
	, bPreviousShowMouseCursor(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UTopDownPawnComponent::BeginPlay()
{
	Super::BeginPlay();

	APawn* Pawn = GetPawn<APawn>();
	if (!ensure(Pawn))
	{
		return;
	}

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

	Super::EndPlay(EndPlayReason);
}

void UTopDownPawnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Pawn = GetPawn<APawn>();
	if (!bHasMoveTarget || !Pawn || !Pawn->IsLocallyControlled())
	{
		CancelMoveToTarget();
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
	SetComponentTickEnabled(false);
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

	const ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	const ULyraPawnData* PawnData = PawnExtension ? PawnExtension->GetPawnData<ULyraPawnData>() : nullptr;
	const ULyraInputConfig* InputConfig = PawnData ? PawnData->InputConfig : nullptr;
	UEnhancedInputComponent* InputComponent = Pawn->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputConfig || !InputComponent || !ClickInputTag.IsValid())
	{
		return;
	}

	const UInputAction* ClickAction = nullptr;
	for (const FLyraInputAction& NativeAction : InputConfig->NativeInputActions)
	{
		if (NativeAction.InputAction && NativeAction.InputTag == ClickInputTag)
		{
			ClickAction = NativeAction.InputAction;
			break;
		}
	}

	if (!ClickAction)
	{
		UE_LOG(LogTopDownPawnComponent, Warning,
			TEXT("PawnData InputConfig '%s' has no native action for '%s'; click movement is disabled for '%s'."),
			*GetNameSafe(InputConfig), *ClickInputTag.ToString(), *GetNameSafe(Pawn));
		return;
	}

	InputBindingHandles.Add(
		InputComponent->BindAction(ClickAction, ETriggerEvent::Started, this, &ThisClass::Input_TopDownClick).GetHandle());
	BoundInputComponent = InputComponent;
	bInputBound = true;

	if (bShowMouseCursor)
	{
		CursorController = PlayerController;
		bPreviousShowMouseCursor = PlayerController->ShouldShowMouseCursor();
		PlayerController->SetShowMouseCursor(true);
	}
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

	if (CursorController && bShowMouseCursor)
	{
		CursorController->SetShowMouseCursor(bPreviousShowMouseCursor);
	}

	BoundInputComponent = nullptr;
	CursorController = nullptr;
	bInputBound = false;
}

void UTopDownPawnComponent::Input_TopDownClick(const FInputActionValue& InputActionValue)
{
	SetMoveTargetUnderCursor();
}
