// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/OceanAdventureAssetLibrary.h"

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "AbilitySystem/LyraAbilitySet.h"
#include "GameFeatureAction_AddComponents.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * Re-running an authoring script must replace the action it created last time instead of
	 * appending a second copy, so callers name their actions and any previous holder of that
	 * name is moved aside first.
	 */
	void ClearExistingActionName(UObject* Outer, FName ActionName)
	{
		if (ActionName.IsNone())
		{
			return;
		}

		if (UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), Outer, ActionName))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}
}

#include "GameFeatures/GameFeatureAction_AddInputBinding.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Input/LyraInputConfig.h"
#include "InputMappingContext.h"
#include "OceanAdventureRuntimeModule.h"

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddComponentsAction(
	UObject* Outer,
	const TArray<TSubclassOf<AActor>>& ActorClasses,
	const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
	bool bClientComponent,
	bool bServerComponent,
	FName ActionName)
{
	if (!IsValid(Outer))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Components action without a valid outer"));
		return nullptr;
	}

	if (ActorClasses.Num() != ComponentClasses.Num())
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("Cannot create Add Components action: %d actor classes but %d component classes"),
			ActorClasses.Num(),
			ComponentClasses.Num());
		return nullptr;
	}

	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddComponents* Action = NewObject<UGameFeatureAction_AddComponents>(Outer, ActionName);
	for (int32 Index = 0; Index < ActorClasses.Num(); ++Index)
	{
		if (!ActorClasses[Index] || !ComponentClasses[Index])
		{
			UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Components action: entry %d has a null class"), Index);
			return nullptr;
		}

		FGameFeatureComponentEntry& Entry = Action->ComponentList.AddDefaulted_GetRef();
		Entry.ActorClass = ActorClasses[Index].Get();
		Entry.ComponentClass = ComponentClasses[Index].Get();
		Entry.bClientComponent = bClientComponent;
		Entry.bServerComponent = bServerComponent;
	}

	return Action;
}

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddInputContextMappingAction(
	UObject* Outer,
	UInputMappingContext* InputMapping,
	int32 Priority,
	FName ActionName)
{
	if (!IsValid(Outer) || !IsValid(InputMapping))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Input Context Mapping action without a valid outer and mapping"));
		return nullptr;
	}

	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddInputContextMapping* Action =
		NewObject<UGameFeatureAction_AddInputContextMapping>(Outer, ActionName);
	FInputMappingContextAndPriority& Entry = Action->InputMappings.AddDefaulted_GetRef();
	Entry.InputMapping = InputMapping;
	Entry.Priority = Priority;
	return Action;
}

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddInputBindingAction(
	UObject* Outer,
	const TArray<ULyraInputConfig*>& InputConfigs,
	FName ActionName)
{
	if (!IsValid(Outer) || InputConfigs.IsEmpty())
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Input Binding action without a valid outer and configs"));
		return nullptr;
	}

	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddInputBinding* Action = NewObject<UGameFeatureAction_AddInputBinding>(Outer, ActionName);
	for (const ULyraInputConfig* Config : InputConfigs)
	{
		if (Config)
		{
			Action->InputConfigs.Add(Config);
		}
	}
	return Action;
}

bool UOceanAdventureAssetLibrary::ConfigureHelmInputMapping(
	UInputMappingContext* InputMapping,
	UInputAction* ThrottleAction,
	UInputAction* SteerAction)
{
	if (!IsValid(InputMapping) || !IsValid(ThrottleAction) || !IsValid(SteerAction))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot configure helm mapping with null assets"));
		return false;
	}

	InputMapping->Modify();
	InputMapping->UnmapAllKeysFromAction(ThrottleAction);
	InputMapping->UnmapAllKeysFromAction(SteerAction);

	// Use FKey names instead of runtime EKeys checks. This function only authors the asset;
	// the runtime component receives Enhanced Input values and never reads physical keys.
	InputMapping->MapKey(ThrottleAction, FKey(FName(TEXT("W"))));
	FEnhancedActionKeyMapping& ThrottleReverse =
		InputMapping->MapKey(ThrottleAction, FKey(FName(TEXT("S"))));
	ThrottleReverse.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));

	InputMapping->MapKey(SteerAction, FKey(FName(TEXT("D"))));
	FEnhancedActionKeyMapping& SteerLeft =
		InputMapping->MapKey(SteerAction, FKey(FName(TEXT("A"))));
	SteerLeft.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));

	const TArray<FEnhancedActionKeyMapping>& Mappings = InputMapping->GetMappings();
	const bool bHasThrottleForward = Mappings.ContainsByPredicate(
		[ThrottleAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == ThrottleAction && Mapping.Key == FKey(FName(TEXT("W")));
		});
	const bool bHasThrottleReverse = Mappings.ContainsByPredicate(
		[ThrottleAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == ThrottleAction && Mapping.Key == FKey(FName(TEXT("S")))
				&& Mapping.Modifiers.Num() == 1
				&& Mapping.Modifiers[0] && Mapping.Modifiers[0]->IsA<UInputModifierNegate>();
		});
	const bool bHasSteerRight = Mappings.ContainsByPredicate(
		[SteerAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == SteerAction && Mapping.Key == FKey(FName(TEXT("D")));
		});
	const bool bHasSteerLeft = Mappings.ContainsByPredicate(
		[SteerAction](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == SteerAction && Mapping.Key == FKey(FName(TEXT("A")))
				&& Mapping.Modifiers.Num() == 1
				&& Mapping.Modifiers[0] && Mapping.Modifiers[0]->IsA<UInputModifierNegate>();
		});

	if (!bHasThrottleForward || !bHasThrottleReverse || !bHasSteerRight || !bHasSteerLeft)
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Helm input mapping did not retain W/S/D/A signed bindings"));
		return false;
	}

	InputMapping->MarkPackageDirty();
	return true;
}

bool UOceanAdventureAssetLibrary::ConfigureDirectHelmInputMapping(
	UInputMappingContext* InputMapping,
	UInputAction* DirectMoveAction)
{
	if (!IsValid(InputMapping) || !IsValid(DirectMoveAction))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot configure direct helm mapping with null assets"));
		return false;
	}

	InputMapping->Modify();
	InputMapping->UnmapAllKeysFromAction(DirectMoveAction);

	FEnhancedActionKeyMapping& MoveForward =
		InputMapping->MapKey(DirectMoveAction, FKey(FName(TEXT("W"))));
	UInputModifierSwizzleAxis* ForwardSwizzle =
		NewObject<UInputModifierSwizzleAxis>(InputMapping);
	ForwardSwizzle->Order = EInputAxisSwizzle::YXZ;
	MoveForward.Modifiers.Add(ForwardSwizzle);

	FEnhancedActionKeyMapping& MoveBackward =
		InputMapping->MapKey(DirectMoveAction, FKey(FName(TEXT("S"))));
	MoveBackward.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));
	UInputModifierSwizzleAxis* BackwardSwizzle =
		NewObject<UInputModifierSwizzleAxis>(InputMapping);
	BackwardSwizzle->Order = EInputAxisSwizzle::YXZ;
	MoveBackward.Modifiers.Add(BackwardSwizzle);

	InputMapping->MapKey(DirectMoveAction, FKey(FName(TEXT("D"))));
	FEnhancedActionKeyMapping& MoveLeft =
		InputMapping->MapKey(DirectMoveAction, FKey(FName(TEXT("A"))));
	MoveLeft.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));

	const TArray<FEnhancedActionKeyMapping>& Mappings = InputMapping->GetMappings();
	auto HasModifier = [](const FEnhancedActionKeyMapping& Mapping, const UClass* ModifierClass)
	{
		return Mapping.Modifiers.ContainsByPredicate(
			[ModifierClass](const TObjectPtr<UInputModifier>& Modifier)
			{
				return Modifier && Modifier->IsA(ModifierClass);
			});
	};
	auto HasMapping = [&Mappings, DirectMoveAction, &HasModifier](
		const TCHAR* KeyName,
		bool bNeedsNegate,
		bool bNeedsSwizzle)
	{
		return Mappings.ContainsByPredicate(
			[DirectMoveAction, KeyName, bNeedsNegate, bNeedsSwizzle, &HasModifier](
				const FEnhancedActionKeyMapping& Mapping)
			{
				return Mapping.Action == DirectMoveAction
					&& Mapping.Key == FKey(FName(KeyName))
					&& HasModifier(Mapping, UInputModifierNegate::StaticClass()) == bNeedsNegate
					&& HasModifier(Mapping, UInputModifierSwizzleAxis::StaticClass()) == bNeedsSwizzle;
			});
	};

	if (!HasMapping(TEXT("W"), false, true)
		|| !HasMapping(TEXT("S"), true, true)
		|| !HasMapping(TEXT("D"), false, false)
		|| !HasMapping(TEXT("A"), true, false))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Direct helm input mapping did not retain Axis2D W/S/D/A bindings"));
		return false;
	}

	InputMapping->MarkPackageDirty();
	return true;
}

bool UOceanAdventureAssetLibrary::ConfigureAbilitySetGameplayAbilities(
	ULyraAbilitySet* AbilitySet,
	const TArray<TSubclassOf<ULyraGameplayAbility>>& AbilityClasses,
	const TArray<int32>& AbilityLevels,
	const TArray<FGameplayTag>& InputTags)
{
	if (!IsValid(AbilitySet)
		|| AbilityClasses.Num() != AbilityLevels.Num()
		|| AbilityClasses.Num() != InputTags.Num())
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("Cannot configure AbilitySet: invalid asset or mismatched arrays (%d classes, %d levels, %d tags)"),
			AbilityClasses.Num(),
			AbilityLevels.Num(),
			InputTags.Num());
		return false;
	}

	FArrayProperty* GrantedAbilitiesProperty = FindFProperty<FArrayProperty>(
		ULyraAbilitySet::StaticClass(),
		TEXT("GrantedGameplayAbilities"));
	const FStructProperty* EntryProperty = GrantedAbilitiesProperty
		? CastField<FStructProperty>(GrantedAbilitiesProperty->Inner)
		: nullptr;
	if (!GrantedAbilitiesProperty
		|| !EntryProperty
		|| EntryProperty->Struct != FLyraAbilitySet_GameplayAbility::StaticStruct())
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("Cannot configure AbilitySet: Lyra GrantedGameplayAbilities reflection layout changed"));
		return false;
	}

	for (int32 Index = 0; Index < AbilityClasses.Num(); ++Index)
	{
		if (!AbilityClasses[Index] || !InputTags[Index].IsValid() || AbilityLevels[Index] < 1)
		{
			UE_LOG(
				LogOceanAdventure,
				Error,
				TEXT("Cannot configure AbilitySet: entry %d has an invalid class, level, or InputTag"),
				Index);
			return false;
		}
	}

	AbilitySet->Modify();
	void* ArrayValue = GrantedAbilitiesProperty->ContainerPtrToValuePtr<void>(AbilitySet);
	FScriptArrayHelper ArrayHelper(GrantedAbilitiesProperty, ArrayValue);
	ArrayHelper.EmptyValues(AbilityClasses.Num());
	for (int32 Index = 0; Index < AbilityClasses.Num(); ++Index)
	{
		const int32 EntryIndex = ArrayHelper.AddValue();
		FLyraAbilitySet_GameplayAbility* Entry =
			reinterpret_cast<FLyraAbilitySet_GameplayAbility*>(ArrayHelper.GetRawPtr(EntryIndex));
		Entry->Ability = AbilityClasses[Index];
		Entry->AbilityLevel = AbilityLevels[Index];
		Entry->InputTag = InputTags[Index];
	}

	AbilitySet->MarkPackageDirty();
	return true;
}
