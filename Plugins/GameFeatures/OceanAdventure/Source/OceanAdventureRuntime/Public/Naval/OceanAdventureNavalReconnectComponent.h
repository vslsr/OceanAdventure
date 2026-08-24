// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameStateComponent.h"
#include "GameFramework/OnlineReplStructs.h"
#include "UObject/WeakObjectPtr.h"

#include "OceanAdventureNavalReconnectComponent.generated.h"

class AController;
class AGameModeBase;
class APlayerState;

/**
 * Where a player was standing when they dropped, and nothing else.
 *
 * This deliberately does not record that they were operating anything. Lyra derives from
 * AGameModeBase, which has none of AGameMode's InactivePlayerArray / OverridePlayerState
 * reconnect machinery, so a reconnecting player is a brand new controller, player state,
 * pawn and ability system component. Anything meant to survive the drop has to be stored
 * somewhere that outlives the player state -- and the corollary is that state you do not
 * want restored is best not stored at all.
 *
 * A position is enough. At the moment of the drop the character is attached at the station's
 * operator point, so "where they were" already means "next to the gun", and because the
 * occupancy relationship was never written down it cannot be accidentally restored: the
 * reconnecting player has to walk up and take the gun again through the same ability, with
 * the same checks, and loses the race if a team mate took it meanwhile.
 */
USTRUCT()
struct FOceanAdventureNavalReconnectAnchor
{
	GENERATED_BODY()

	/** The only key that survives a reconnect: the player id is reassigned, the name is not unique. */
	UPROPERTY()
	FUniqueNetIdRepl PlayerId;

	/** Development-only fallback, because PIE clients can come up without a stable net id. */
	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	int32 TeamId = INDEX_NONE;

	/** The vessel that was under the player. Null and unset means they were on ground or in water. */
	UPROPERTY()
	TWeakObjectPtr<AActor> Vessel;

	UPROPERTY()
	bool bVesselRelative = false;

	/**
	 * Vessel-space when they were aboard, world-space otherwise.
	 *
	 * A ship under way covers hundreds of metres during a thirty second reconnect, so a world
	 * position would drop the returning player into open water behind it. This is the same
	 * reason a station attaches its operator instead of teleporting them every frame.
	 */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	double ExpireServerTime = 0.0;

	/** False when the ship the anchor was relative to has since sunk and been destroyed. */
	bool ResolveWorldLocation(FVector& OutLocation) const;
};

/**
 * The anchor table, on the game state.
 *
 * The game state is the only thing here that lives for the whole match: a player state dies
 * with the connection, a pawn dies with it too, and a cannon can be blown apart or change
 * hands while its former operator is away.
 */
UCLASS(BlueprintType, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalReconnectComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureNavalReconnectComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Records where a leaving controller's pawn is standing. Server side.
	 *
	 * Logout is the only moment this works: the controller, its player state and its pawn are
	 * all still alive. It is also, conveniently, the moment before the pawn is destroyed --
	 * which is exactly what the station's own release path is waiting for. Recording needs
	 * the pawn present and freeing the seat needs it gone, and the natural Logout ordering
	 * satisfies both without any coordination between them.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	void RecordAnchor(AController* Exiting);

	/** Non-destructive lookup. False when there is no anchor or it has expired. */
	bool FindAnchor(const AController* Player, FOceanAdventureNavalReconnectAnchor& OutAnchor) const;

	/**
	 * Drops the player's anchor, whether or not it was used.
	 *
	 * Mandatory at every use site: OnFinishRestartPlayer runs on ordinary deaths too, so an
	 * anchor left in the table teleports the player back to their disconnect position every
	 * time they respawn for the rest of the match.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	bool ConsumeAnchor(const AController* Player);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	void ClearAllAnchors();

protected:
	/**
	 * How long an anchor is honoured. A P0 match is eight minutes, so anything beyond a
	 * couple of minutes is a different session in spirit and the player starts fresh.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0", Units = "s"))
	float AnchorLifetimeSeconds = 120.0f;

private:
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);
	int32 FindAnchorIndex(const APlayerState* PlayerState) const;
	void PruneExpired();

	UPROPERTY()
	TArray<FOceanAdventureNavalReconnectAnchor> Anchors;

	FDelegateHandle LogoutHandle;
};
