// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OceanAdventureScriptTagLibrary.generated.h"

/**
 * Gameplay tags, in a form a script can actually use.
 *
 * FGameplayTag is a struct wrapped around a registry entry, and its useful members are plain
 * C++ rather than reflected -- so a script that receives one can neither read it nor build
 * one. Without these four calls every tag in the boundary would be write-only, which would
 * quietly make refusal reasons and cue tags unusable from TypeScript.
 *
 * MakeTag refuses to invent tags. Requesting an unregistered name returns an invalid tag and
 * says so, rather than creating one on the fly: a typo that silently became a real tag would
 * match nothing anywhere else in the project and look exactly like a rule that never ran.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureScriptTagLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Looks a tag up in the registry. Returns an invalid tag, and warns, when it is unknown. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Tags")
	static FGameplayTag MakeTag(FName TagName);

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Tags")
	static FString TagToString(FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Tags")
	static bool IsTagValid(FGameplayTag Tag);

	/**
	 * Hierarchical match: Naval.Fail.Mounted matches the parent Naval.Fail.
	 *
	 * Exposed instead of leaving scripts to compare strings, because string comparison gets
	 * the hierarchy wrong in the direction that looks like it works -- a prefix test passes
	 * for Naval.Failure too.
	 */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Tags")
	static bool TagMatches(FGameplayTag Tag, FGameplayTag Parent);
};
