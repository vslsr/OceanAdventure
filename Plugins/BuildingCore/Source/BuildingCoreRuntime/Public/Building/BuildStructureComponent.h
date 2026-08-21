// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "Building/BuildResourceSource.h"
#include "Building/BuildStructureHost.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "BuildStructureComponent.generated.h"

class AController;
class UBuildCreativeResourceSource;
class UBuildPieceCatalog;
class UBuildPieceDefinition;
class UBuildStructureComponent;

USTRUCT()
struct BUILDINGCORERUNTIME_API FBuildPieceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FBuildSlotKey Key;

	UPROPERTY()
	uint16 PieceIndex = 0;

	UPROPERTY()
	uint8 Rotation = 0;

	void PostReplicatedAdd(const struct FBuildPieceList& InArraySerializer);
	void PostReplicatedRemove(const struct FBuildPieceList& InArraySerializer);
	void PostReplicatedChange(const struct FBuildPieceList& InArraySerializer);
};

USTRUCT()
struct BUILDINGCORERUNTIME_API FBuildPieceList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FBuildPieceEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UBuildStructureComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FBuildPieceEntry, FBuildPieceList>(
			Entries, DeltaParameters, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FBuildPieceList> : public TStructOpsTypeTraitsBase2<FBuildPieceList>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

DECLARE_MULTICAST_DELEGATE(FOnBuildStructureChanged);

/** Server-authoritative structure truth. It is intentionally independent of GAS and LyraGame. */
UCLASS(BlueprintType, ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class BUILDINGCORERUNTIME_API UBuildStructureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildStructureComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Building")
	bool CanPlacePiece(
		const FBuildSlotKey& Key,
		const UBuildPieceDefinition* Definition,
		FGameplayTag& OutFailReason) const;

	UFUNCTION(BlueprintPure, Category = "Building")
	const UBuildPieceDefinition* QueryPiece(const FBuildSlotKey& Key) const;

	UFUNCTION(BlueprintPure, Category = "Building")
	FBuildSlotKey WorldToSlot(const FVector& WorldLocation, EBuildSlotType Slot) const;

	UFUNCTION(BlueprintPure, Category = "Building")
	FVector SlotToWorld(const FBuildSlotKey& Key) const;

	FTransform GetPieceRelativeTransform(
		const FBuildSlotKey& Key,
		const UBuildPieceDefinition* Definition,
		uint8 Rotation) const;

	FBox ComputeLocalStructureBounds() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
	bool TryPlacePiece(
		const FBuildSlotKey& Key,
		const UBuildPieceDefinition* Definition,
		uint8 Rotation,
		AController* Instigator);

	bool TryPlacePieceWithReason(
		const FBuildSlotKey& Key,
		const UBuildPieceDefinition* Definition,
		uint8 Rotation,
		AController* Instigator,
		FGameplayTag& OutFailReason);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
	bool TryRemovePiece(const FBuildSlotKey& Key, AController* Instigator);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
	int32 ClearAllPieces();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
	void SetResourceSource(UObject* NewResourceSource);

	UFUNCTION(BlueprintCallable, Category = "Building|Debug")
	void SetDebugDrawingEnabled(bool bEnabled);

	void SetPieceCatalog(UBuildPieceCatalog* NewCatalog);

	const TArray<FBuildPieceEntry>& GetEntries() const { return PieceList.Entries; }
	const UBuildPieceCatalog* GetCatalog() const { return PieceCatalog; }
	const TSet<FBuildGridCoord>& GetAnchorCells() const { return AnchorCells; }
	const FBuildGridSettings& GetGridSettings() const { return GridSettings; }
	IBuildStructureHost* GetHost() const { return Host.GetInterface(); }

	FOnBuildStructureChanged OnStructureChanged;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building")
	TObjectPtr<UBuildPieceCatalog> PieceCatalog;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	int32 MaxPieceCount = 500;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "0.0", Units = "cm"))
	double MaxPlacementDistance = 1500.0;

	UPROPERTY(Replicated)
	FBuildPieceList PieceList;

private:
	bool ResolveHost();
	bool IsAnchored(const FBuildGridCoord& Coord) const;
	void RebuildAnchorCells();
	void RebuildIndex();
	bool CheckSupport(const FBuildSlotKey& Key, const UBuildPieceDefinition* Definition) const;
	bool HasPieceAt(const FBuildGridCoord& Coord, EBuildSlotType Slot) const;
	bool IsCellBlockedByPawn(const FBuildGridCoord& Coord) const;
	bool WouldStayConnectedWithout(int32 SkipEntryIndex) const;
	bool IsInstigatorWithinRange(const FBuildSlotKey& Key, const AController* Instigator) const;
	void NotifyStructureChanged();
	void DrawDebugStructure() const;

	friend struct FBuildPieceEntry;
	void HandleEntriesReplicated();

	TMap<FBuildSlotKey, int32> SlotToEntryIndex;
	TSet<FBuildGridCoord> AnchorCells;
	TScriptInterface<IBuildStructureHost> Host;
	FBuildGridSettings GridSettings;

	UPROPERTY(Transient)
	TScriptInterface<IBuildResourceSource> ResourceSource;

	UPROPERTY(Transient)
	TObjectPtr<UBuildCreativeResourceSource> CreativeResourceSource;

	bool bRebuildQueued = false;
	bool bDrawDebug = false;
};
