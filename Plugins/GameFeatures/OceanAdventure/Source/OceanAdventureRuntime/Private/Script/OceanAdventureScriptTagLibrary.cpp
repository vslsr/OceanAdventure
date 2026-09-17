// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/OceanAdventureScriptTagLibrary.h"

#include "GameplayTagsManager.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureScriptTagLibrary)

FGameplayTag UOceanAdventureScriptTagLibrary::MakeTag(FName TagName)
{
	if (TagName.IsNone())
	{
		return FGameplayTag();
	}

	// ErrorIfNotFound=false: this reports the miss itself, with the name the script asked for,
	// because the script author never sees the engine's own message.
	const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, /*ErrorIfNotFound=*/false);

	if (!Tag.IsValid())
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[Script] Unknown gameplay tag '%s'. Tags are registered in C++ or in the tag tables; ")
			TEXT("a script cannot create one."),
			*TagName.ToString());
	}

	return Tag;
}

FString UOceanAdventureScriptTagLibrary::TagToString(FGameplayTag Tag)
{
	return Tag.ToString();
}

bool UOceanAdventureScriptTagLibrary::IsTagValid(FGameplayTag Tag)
{
	return Tag.IsValid();
}

bool UOceanAdventureScriptTagLibrary::TagMatches(FGameplayTag Tag, FGameplayTag Parent)
{
	return Tag.IsValid() && Parent.IsValid() && Tag.MatchesTag(Parent);
}
