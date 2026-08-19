// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/OceanAdventurePawn.h"

#include "Character/LyraPawnData.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventurePawn)

AOceanAdventurePawn::AOceanAdventurePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	check(MovementComponent);

	// Starting values only. Tune them on the pawn blueprint's CharacterMovement component
	// rather than here, so designers do not need a recompile.
	MovementComponent->GetNavAgentPropertiesRef().bCanSwim = true;
	MovementComponent->MaxSwimSpeed = 600.0f;
	MovementComponent->Buoyancy = 1.0f;
}

void AOceanAdventurePawn::BeginPlay()
{
	Super::BeginPlay();

#if !NO_LOGGING
	// Injected components only exist at runtime, so list them here: this is the quickest way
	// to confirm the experience actually activated the features and their actions ran.
	if (!LogOceanAdventure.IsSuppressed(ELogVerbosity::Verbose))
	{
		TArray<FString> ComponentNames;
		for (const UActorComponent* Component : GetComponents())
		{
			if (Component)
			{
				ComponentNames.Add(Component->GetClass()->GetName());
			}
		}
		ComponentNames.Sort();

		UE_LOG(LogOceanAdventure, Verbose, TEXT("%s components: %s"),
			*GetName(), *FString::Join(ComponentNames, TEXT(", ")));
	}
#endif // !NO_LOGGING
}

const ULyraPawnData* AOceanAdventurePawn::GetOceanAdventurePawnData() const
{
	if (const ULyraPawnExtensionComponent* PawnExtComp = ULyraPawnExtensionComponent::FindPawnExtensionComponent(this))
	{
		return PawnExtComp->GetPawnData<ULyraPawnData>();
	}

	return nullptr;
}
