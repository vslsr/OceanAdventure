// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalReconnectComponent.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalTimeStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalReconnectComponent)

bool FOceanAdventureNavalReconnectAnchor::ResolveWorldLocation(FVector& OutLocation) const
{
	if (!bVesselRelative)
	{
		OutLocation = Location;
		return true;
	}

	// The ship sank and was cleaned up while the player was away. There is no meaningful
	// place to put them back, so the caller falls through to an ordinary player start.
	const AActor* VesselActor = Vessel.Get();
	if (!IsValid(VesselActor))
	{
		return false;
	}

	OutLocation = VesselActor->GetActorTransform().TransformPosition(Location);
	return true;
}

UOceanAdventureNavalReconnectComponent::UOceanAdventureNavalReconnectComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Server-only bookkeeping. Nothing here is worth a byte of bandwidth: the client learns
	// where it came back by being put there.
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = false;
}

void UOceanAdventureNavalReconnectComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// The engine's global logout event rather than a game mode subclass: this component is
	// injected by an Experience, and requiring a bespoke game mode alongside it would put the
	// same feature in two places that have to agree.
	LogoutHandle = FGameModeEvents::OnGameModeLogoutEvent().AddUObject(this, &ThisClass::HandleLogout);
}

void UOceanAdventureNavalReconnectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LogoutHandle.IsValid())
	{
		FGameModeEvents::OnGameModeLogoutEvent().Remove(LogoutHandle);
		LogoutHandle.Reset();
	}
	Anchors.Reset();

	Super::EndPlay(EndPlayReason);
}

void UOceanAdventureNavalReconnectComponent::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	// The event is global to the process, so a second PIE world's logout would otherwise be
	// recorded in this world's table.
	if (!GameMode || GameMode->GetWorld() != GetWorld())
	{
		return;
	}

	RecordAnchor(Exiting);
}

void UOceanAdventureNavalReconnectComponent::RecordAnchor(AController* Exiting)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Exiting)
	{
		return;
	}

	APlayerState* PlayerState = Exiting->PlayerState;
	APawn* Pawn = Exiting->GetPawn();
	if (!PlayerState || PlayerState->IsABot() || PlayerState->IsOnlyASpectator() || !IsValid(Pawn))
	{
		return;
	}

	PruneExpired();

	FOceanAdventureNavalReconnectAnchor Anchor;
	Anchor.PlayerId = PlayerState->GetUniqueId();
	Anchor.PlayerName = PlayerState->GetPlayerName();
	Anchor.TeamId = NavalTeam::GetTeamId(Pawn);
	Anchor.ExpireServerTime =
		NavalTime::GetNetworkTimeSeconds(this) + static_cast<double>(AnchorLifetimeSeconds);

	const FVector PawnLocation = Pawn->GetActorLocation();
	const UNavalVesselComponent* Vessel = UOceanAdventureNavalStatics::FindVesselUnderPawn(Pawn);
	AActor* VesselActor = Vessel ? Vessel->GetOwner() : nullptr;
	if (IsValid(VesselActor))
	{
		Anchor.Vessel = VesselActor;
		Anchor.bVesselRelative = true;
		Anchor.Location = VesselActor->GetActorTransform().InverseTransformPosition(PawnLocation);
	}
	else
	{
		Anchor.Location = PawnLocation;
	}

	// One anchor per player: a reconnect that drops again replaces the earlier one.
	const int32 ExistingIndex = FindAnchorIndex(PlayerState);
	if (ExistingIndex != INDEX_NONE)
	{
		Anchors[ExistingIndex] = Anchor;
	}
	else
	{
		Anchors.Add(Anchor);
	}

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[NavalReconnect] Anchor recorded player=%s team=%d vessel=%s location=%s expires_in=%.1f"),
		*Anchor.PlayerName,
		Anchor.TeamId,
		*GetNameSafe(VesselActor),
		*Anchor.Location.ToCompactString(),
		AnchorLifetimeSeconds);
}

bool UOceanAdventureNavalReconnectComponent::FindAnchor(
	const AController* Player, FOceanAdventureNavalReconnectAnchor& OutAnchor) const
{
	const APlayerState* PlayerState = Player ? Player->PlayerState : nullptr;
	const int32 Index = FindAnchorIndex(PlayerState);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	if (Anchors[Index].ExpireServerTime <= NavalTime::GetNetworkTimeSeconds(this))
	{
		return false;
	}

	OutAnchor = Anchors[Index];
	return true;
}

bool UOceanAdventureNavalReconnectComponent::ConsumeAnchor(const AController* Player)
{
	const APlayerState* PlayerState = Player ? Player->PlayerState : nullptr;
	const int32 Index = FindAnchorIndex(PlayerState);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Anchors.RemoveAtSwap(Index);
	return true;
}

void UOceanAdventureNavalReconnectComponent::ClearAllAnchors()
{
	Anchors.Reset();
}

int32 UOceanAdventureNavalReconnectComponent::FindAnchorIndex(const APlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return INDEX_NONE;
	}

	const FUniqueNetIdRepl& PlayerId = PlayerState->GetUniqueId();
	if (PlayerId.IsValid())
	{
		for (int32 Index = 0; Index < Anchors.Num(); ++Index)
		{
			if (Anchors[Index].PlayerId.IsValid() && Anchors[Index].PlayerId == PlayerId)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

#if !UE_BUILD_SHIPPING
	// PIE clients and some standalone setups come up without a stable net id, which would
	// make this whole feature untestable outside a real dedicated server. Names are neither
	// unique nor immutable, so this stays out of shipping builds.
	const FString PlayerName = PlayerState->GetPlayerName();
	if (!PlayerName.IsEmpty())
	{
		for (int32 Index = 0; Index < Anchors.Num(); ++Index)
		{
			if (!Anchors[Index].PlayerId.IsValid() && Anchors[Index].PlayerName == PlayerName)
			{
				return Index;
			}
		}
	}
#endif

	return INDEX_NONE;
}

void UOceanAdventureNavalReconnectComponent::PruneExpired()
{
	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	Anchors.RemoveAllSwap(
		[Now](const FOceanAdventureNavalReconnectAnchor& Anchor)
		{
			return Anchor.ExpireServerTime <= Now;
		});
}
