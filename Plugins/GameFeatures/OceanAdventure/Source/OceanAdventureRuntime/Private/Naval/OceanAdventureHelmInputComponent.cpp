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
	, HelmMappingContext(FSoftObjectPath(TEXT("/OceanAdventure/Input/IMC_OceanHelm.IMC_OceanHelm")))
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
			TEXT("[NavalInputTrace] phase=bind result=missing-dependency pawn=%s controller=%s input_component=%s throttle_tag=%s throttle_action=%s steer_tag=%s steer_action=%s input_config=%s"),
			*GetNameSafe(Pawn), *GetNameSafe(PlayerController), *GetNameSafe(InputComponent),
			*ThrottleInputTag.ToString(), *GetNameSafe(ThrottleAction),
			*SteerInputTag.ToString(), *GetNameSafe(SteerAction), *GetNameSafe(InputConfig));
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
	UE_LOG(LogOceanAdventureHelmInput, Display,
		TEXT("[NavalInputTrace] phase=bind result=success pawn=%s controller=%s input_component=%s input_config=%s throttle_tag=%s throttle_action=%s steer_tag=%s steer_action=%s binding_count=%d"),
		*GetNameSafe(Pawn), *GetNameSafe(PlayerController), *GetNameSafe(InputComponent),
		*GetNameSafe(InputConfig), *ThrottleInputTag.ToString(), *GetNameSafe(ThrottleAction),
		*SteerInputTag.ToString(), *GetNameSafe(SteerAction), InputBindingHandles.Num());
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
		UE_LOG(LogOceanAdventureHelmInput, Verbose,
			TEXT("[NavalInputTrace] phase=mapping-enable result=already-enabled pawn=%s"),
			*GetNameSafe(GetPawn<APawn>()));
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
			TEXT("[NavalInputTrace] phase=mapping-enable result=input-not-bound pawn=%s"),
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
			TEXT("[NavalInputTrace] phase=mapping-enable result=missing-dependency pawn=%s controller=%s local_player=%s subsystem=%s mapping=%s soft_path=%s"),
			*GetNameSafe(Pawn), *GetNameSafe(PlayerController), *GetNameSafe(LocalPlayer),
			*GetNameSafe(InputSubsystem), *GetNameSafe(MappingContext),
			*HelmMappingContext.ToSoftObjectPath().ToString());
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
		TEXT("[NavalInputTrace] phase=mapping-enable result=success context=%s priority=%d pawn=%s controller=%s input_bound=%d"),
		*GetNameSafe(MappingContext), HelmMappingPriority, *GetNameSafe(Pawn),
		*GetNameSafe(PlayerController), bInputBound);
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
	UE_LOG(LogOceanAdventureHelmInput, Display,
		TEXT("[NavalInputTrace] phase=mapping-disable result=complete pawn=%s controller=%s subsystem=%s"),
		*GetNameSafe(Pawn), *GetNameSafe(PlayerController), *GetNameSafe(InputSubsystem));
}

void UOceanAdventureHelmInputComponent::ResetInput()
{
	ThrottleInput = 0.0f;
	SteerInput = 0.0f;
}

void UOceanAdventureHelmInputComponent::Input_Throttle(const FInputActionValue& InputActionValue)
{
	const float NewValue = FMath::Clamp(InputActionValue.Get<float>(), -1.0f, 1.0f);
	if (!FMath::IsNearlyEqual(ThrottleInput, NewValue))
	{
		UE_LOG(LogOceanAdventureHelmInput, Display,
			TEXT("[NavalInputTrace] phase=helm-action action=throttle event=triggered pawn=%s previous=%.3f value=%.3f input_enabled=%d input_bound=%d"),
			*GetNameSafe(GetPawn<APawn>()), ThrottleInput, NewValue, bHelmInputEnabled, bInputBound);
	}
	ThrottleInput = NewValue;
}

void UOceanAdventureHelmInputComponent::Input_Steer(const FInputActionValue& InputActionValue)
{
	const float NewValue = FMath::Clamp(InputActionValue.Get<float>(), -1.0f, 1.0f);
	if (!FMath::IsNearlyEqual(SteerInput, NewValue))
	{
		UE_LOG(LogOceanAdventureHelmInput, Display,
			TEXT("[NavalInputTrace] phase=helm-action action=steer event=triggered pawn=%s previous=%.3f value=%.3f input_enabled=%d input_bound=%d"),
			*GetNameSafe(GetPawn<APawn>()), SteerInput, NewValue, bHelmInputEnabled, bInputBound);
	}
	SteerInput = NewValue;
}

void UOceanAdventureHelmInputComponent::Input_ThrottleReleased(const FInputActionValue& /*InputActionValue*/)
{
	if (!FMath::IsNearlyZero(ThrottleInput))
	{
		UE_LOG(LogOceanAdventureHelmInput, Display,
			TEXT("[NavalInputTrace] phase=helm-action action=throttle event=released pawn=%s previous=%.3f input_enabled=%d input_bound=%d"),
			*GetNameSafe(GetPawn<APawn>()), ThrottleInput, bHelmInputEnabled, bInputBound);
	}
	ThrottleInput = 0.0f;
}

void UOceanAdventureHelmInputComponent::Input_SteerReleased(const FInputActionValue& /*InputActionValue*/)
{
	if (!FMath::IsNearlyZero(SteerInput))
	{
		UE_LOG(LogOceanAdventureHelmInput, Display,
			TEXT("[NavalInputTrace] phase=helm-action action=steer event=released pawn=%s previous=%.3f input_enabled=%d input_bound=%d"),
			*GetNameSafe(GetPawn<APawn>()), SteerInput, bHelmInputEnabled, bInputBound);
	}
	SteerInput = 0.0f;
}
