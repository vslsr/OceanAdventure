// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureHelmInputComponent.h"

#include "Components/GameFrameworkComponentManager.h"
#include "Character/LyraHeroComponent.h"
#include "Character/LyraPawnData.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Input/LyraInputConfig.h"
#include "InputMappingContext.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureHelmInputComponent)

DEFINE_LOG_CATEGORY_STATIC(LogOceanAdventureHelmInput, Log, All);

UOceanAdventureHelmInputComponent::UOceanAdventureHelmInputComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ThrottleInputTag(OceanAdventureNavalTags::InputTag_Naval_Helm_Throttle)
	, SteerInputTag(OceanAdventureNavalTags::InputTag_Naval_Helm_Steer)
	, HelmMappingContext(FSoftObjectPath(TEXT("/OceanAdventure/Input/IMC_OceanHelm")))
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOceanAdventureHelmInputComponent::BeginPlay()
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
			UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
				this, &ThisClass::HandlePawnExtension);
		ExtensionRequestHandle = ComponentManager->AddExtensionHandler(Pawn->GetClass(), ExtensionDelegate);
	}

	// The component can be injected after LyraHeroComponent has already broadcast BindInputsNow.
	BindInputIfReady();
}

void UOceanAdventureHelmInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExtensionRequestHandle.Reset();
	DisableHelmInput();
	UnbindInput();
	Super::EndPlay(EndPlayReason);
}

void UOceanAdventureHelmInputComponent::HandlePawnExtension(AActor* Actor, FName EventName)
{
	if (Actor != GetOwner())
	{
		return;
	}

	if (EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved
		|| EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved)
	{
		DisableHelmInput();
		UnbindInput();
	}
	else if (EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded
		|| EventName == ULyraHeroComponent::NAME_BindInputsNow)
	{
		BindInputIfReady();
	}
}

void UOceanAdventureHelmInputComponent::BindInputIfReady()
{
	if (bInputBound)
	{
		return;
	}

	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn
		? Cast<APlayerController>(Pawn->GetController())
		: nullptr;
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	const ULyraHeroComponent* HeroComponent = ULyraHeroComponent::FindHeroComponent(Pawn);
	if (!HeroComponent || !HeroComponent->IsReadyToBindInputs())
	{
		return;
	}

	const ULyraPawnExtensionComponent* PawnExtension =
		ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	const ULyraPawnData* PawnData = PawnExtension
		? PawnExtension->GetPawnData<ULyraPawnData>()
		: nullptr;
	const ULyraInputConfig* InputConfig = PawnData ? PawnData->InputConfig : nullptr;
	const UInputAction* ThrottleAction = nullptr;
	const UInputAction* SteerAction = nullptr;
	if (InputConfig)
	{
		for (const FLyraInputAction& NativeAction : InputConfig->NativeInputActions)
		{
			if (NativeAction.InputTag == ThrottleInputTag)
			{
				ThrottleAction = NativeAction.InputAction;
			}
			else if (NativeAction.InputTag == SteerInputTag)
			{
				SteerAction = NativeAction.InputAction;
			}
		}
	}

	UEnhancedInputComponent* InputComponent = Pawn->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputComponent || !ThrottleInputTag.IsValid() || !SteerInputTag.IsValid()
		|| !ThrottleAction || !SteerAction)
	{
		UE_LOG(
			LogOceanAdventureHelmInput,
			Warning,
			TEXT("Helm input disabled for %s: component or tagged native actions are missing"),
			*GetNameSafe(Pawn));
		return;
	}

	InputBindingHandles.Add(
		InputComponent->BindAction(
			ThrottleAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Throttle).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(
			ThrottleAction, ETriggerEvent::Completed, this, &ThisClass::Input_ThrottleReleased).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(
			ThrottleAction, ETriggerEvent::Canceled, this, &ThisClass::Input_ThrottleReleased).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(
			SteerAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Steer).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(
			SteerAction, ETriggerEvent::Completed, this, &ThisClass::Input_SteerReleased).GetHandle());
	InputBindingHandles.Add(
		InputComponent->BindAction(
			SteerAction, ETriggerEvent::Canceled, this, &ThisClass::Input_SteerReleased).GetHandle());

	BoundInputComponent = InputComponent;
	bInputBound = true;
	UE_LOG(LogOceanAdventureHelmInput, Verbose, TEXT("Bound helm actions for %s"), *GetNameSafe(Pawn));
}

void UOceanAdventureHelmInputComponent::UnbindInput()
{
	if (BoundInputComponent)
	{
		for (const uint32 Handle : InputBindingHandles)
		{
			BoundInputComponent->RemoveBindingByHandle(Handle);
		}
	}

	InputBindingHandles.Reset();
	BoundInputComponent = nullptr;
	bInputBound = false;
	ResetInput();
}

void UOceanAdventureHelmInputComponent::EnableHelmInput()
{
	if (bHelmInputEnabled)
	{
		return;
	}

	// Component injection and Lyra's BindInputsNow event do not have a guaranteed order.
	// Retry here, at the exact point the ability takes ownership of W/A/S/D, and refuse to
	// claim the mapping if the tagged native actions still cannot be resolved.
	BindInputIfReady();
	if (!bInputBound)
	{
		UE_LOG(
			LogOceanAdventureHelmInput,
			Error,
			TEXT("Cannot enable helm input for %s: tagged native actions are not bound"),
			*GetNameSafe(GetPawn<APawn>()));
		return;
	}

	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn
		? Cast<APlayerController>(Pawn->GetController())
		: nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	const UInputMappingContext* MappingContext = HelmMappingContext.LoadSynchronous();
	if (!InputSubsystem || !MappingContext)
	{
		UE_LOG(
			LogOceanAdventureHelmInput,
			Warning,
			TEXT("Cannot push helm mapping for %s: local input subsystem or IMC_OceanHelm is missing"),
			*GetNameSafe(Pawn));
		return;
	}

	FModifyContextOptions Options = {};
	Options.bIgnoreAllPressedKeysUntilRelease = false;
	InputSubsystem->AddMappingContext(MappingContext, HelmMappingPriority, Options);
	bHelmInputEnabled = true;
	ResetInput();
	UE_LOG(
		LogOceanAdventureHelmInput,
		Display,
		TEXT("Pushed helm mapping context=%s priority=%d pawn=%s"),
		*GetNameSafe(MappingContext), HelmMappingPriority, *GetNameSafe(Pawn));
}

void UOceanAdventureHelmInputComponent::DisableHelmInput()
{
	if (!bHelmInputEnabled)
	{
		ResetInput();
		return;
	}

	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PlayerController = Pawn
		? Cast<APlayerController>(Pawn->GetController())
		: nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	if (InputSubsystem)
	{
		if (const UInputMappingContext* MappingContext = HelmMappingContext.Get())
		{
			InputSubsystem->RemoveMappingContext(MappingContext);
		}
	}

	bHelmInputEnabled = false;
	ResetInput();
	UE_LOG(LogOceanAdventureHelmInput, Display, TEXT("Popped helm mapping pawn=%s"), *GetNameSafe(Pawn));
}

void UOceanAdventureHelmInputComponent::ResetInput()
{
	ThrottleInput = 0.0f;
	SteerInput = 0.0f;
}

void UOceanAdventureHelmInputComponent::Input_Throttle(const FInputActionValue& InputActionValue)
{
	ThrottleInput = FMath::Clamp(InputActionValue.Get<float>(), -1.0f, 1.0f);
}

void UOceanAdventureHelmInputComponent::Input_Steer(const FInputActionValue& InputActionValue)
{
	SteerInput = FMath::Clamp(InputActionValue.Get<float>(), -1.0f, 1.0f);
}

void UOceanAdventureHelmInputComponent::Input_ThrottleReleased(const FInputActionValue& /*InputActionValue*/)
{
	ThrottleInput = 0.0f;
}

void UOceanAdventureHelmInputComponent::Input_SteerReleased(const FInputActionValue& /*InputActionValue*/)
{
	SteerInput = 0.0f;
}
