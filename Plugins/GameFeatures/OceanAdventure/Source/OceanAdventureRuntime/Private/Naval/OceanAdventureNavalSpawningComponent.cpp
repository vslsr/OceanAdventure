// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalSpawningComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"
#include "Naval/NavalSpawnStatics.h"
#include "Naval/OceanAdventureNavalReconnectComponent.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalSpawningComponent)

UOceanAdventureNavalSpawningComponent::UOceanAdventureNavalSpawningComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOceanAdventureNavalSpawningComponent::OnFinishRestartPlayer(
	AController* Player, const FRotator& StartRotation)
{
	Super::OnFinishRestartPlayer(Player, StartRotation);

	AGameStateBase* GameState = GetGameState<AGameStateBase>();
	UOceanAdventureNavalReconnectComponent* Reconnect = GameState
		? GameState->FindComponentByClass<UOceanAdventureNavalReconnectComponent>()
		: nullptr;
	if (!Reconnect || !Player)
	{
		return;
	}

	// This runs on every respawn, not only on a reconnect, so the anchor is consumed on every
	// path out of here. Leaving it behind would drag the player back to their disconnect spot
	// after every death for the rest of the match.
	ON_SCOPE_EXIT
	{
		Reconnect->ConsumeAnchor(Player);
	};

	FOceanAdventureNavalReconnectAnchor Anchor;
	if (!Reconnect->FindAnchor(Player, Anchor))
	{
		return;
	}

	APawn* Pawn = Player->GetPawn();
	if (!IsValid(Pawn))
	{
		return;
	}

	FVector TargetLocation = FVector::ZeroVector;
	if (!Anchor.ResolveWorldLocation(TargetLocation))
	{
		// The ship the anchor hung off is gone. The normal player start is the right answer.
		UE_LOG(LogOceanAdventure, Display,
			TEXT("[NavalReconnect] Anchor vessel is gone, keeping the player start pawn=%s"),
			*GetNameSafe(Pawn));
		return;
	}

	FVector SafeLocation = FVector::ZeroVector;
	if (!UNavalSpawnStatics::FindClearSpotNear(Pawn, TargetLocation, ClearanceRadius, SafeLocation))
	{
		UE_LOG(LogOceanAdventure, Display,
			TEXT("[NavalReconnect] No clear spot near %s, keeping the player start pawn=%s"),
			*TargetLocation.ToCompactString(), *GetNameSafe(Pawn));
		return;
	}

	if (!Pawn->TeleportTo(SafeLocation, Pawn->GetActorRotation()))
	{
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalReconnect] Teleport refused pawn=%s target=%s"),
			*GetNameSafe(Pawn), *SafeLocation.ToCompactString());
		return;
	}

	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalReconnect] Restored pawn=%s to %s (anchor %s, vessel=%s)"),
		*GetNameSafe(Pawn), *SafeLocation.ToCompactString(),
		*Anchor.Location.ToCompactString(), *GetNameSafe(Anchor.Vessel.Get()));
}
