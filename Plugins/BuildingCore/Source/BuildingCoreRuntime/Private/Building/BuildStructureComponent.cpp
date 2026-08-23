// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildStructureComponent.h"

#include "Building/BuildGameplayTags.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureSubsystem.h"
#include "BuildingCoreRuntimeModule.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CheatManagerDefines.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildStructureComponent)

void FBuildPieceEntry::PostReplicatedAdd(const FBuildPieceList& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleEntriesReplicated();
	}
}

void FBuildPieceEntry::PostReplicatedRemove(const FBuildPieceList& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleEntriesReplicated();
	}
}

void FBuildPieceEntry::PostReplicatedChange(const FBuildPieceList& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleEntriesReplicated();
	}
}

UBuildStructureComponent::UBuildStructureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UBuildStructureComponent::BeginPlay()
{
	Super::BeginPlay();

	PieceList.OwnerComponent = this;
	ResolveHost();
	RebuildAnchorCells();
	RebuildIndex();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CreativeResourceSource = NewObject<UBuildCreativeResourceSource>(this);
		SetResourceSource(CreativeResourceSource);
	}

	NotifyStructureChanged();

	if (UWorld* World = GetWorld())
	{
		if (UBuildStructureSubsystem* Registry = World->GetSubsystem<UBuildStructureSubsystem>())
		{
			Registry->RegisterStructure(this);
		}
	}
}

void UBuildStructureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UBuildStructureSubsystem* Registry = World->GetSubsystem<UBuildStructureSubsystem>())
		{
			Registry->UnregisterStructure(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UBuildStructureComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDrawDebug)
	{
		DrawDebugStructure();
	}
}

void UBuildStructureComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBuildStructureComponent, PieceList);
}

bool UBuildStructureComponent::ResolveHost()
{
	AActor* OwnerActor = GetOwner();
	IBuildStructureHost* HostInterface = OwnerActor ? Cast<IBuildStructureHost>(OwnerActor) : nullptr;
	if (!OwnerActor || !HostInterface)
	{
		UE_LOG(
			LogBuildingCore,
			Error,
			TEXT("%s does not implement IBuildStructureHost; building is disabled"),
			*GetNameSafe(OwnerActor));
		Host = nullptr;
		return false;
	}

	Host.SetObject(OwnerActor);
	Host.SetInterface(HostInterface);
	GridSettings = HostInterface->GetGridSettings();
	return true;
}

void UBuildStructureComponent::HandleEntriesReplicated()
{
	if (bRebuildQueued)
	{
		return;
	}

	bRebuildQueued = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				bRebuildQueued = false;
				RebuildIndex();
				NotifyStructureChanged();
			}));
	}
}

void UBuildStructureComponent::RebuildAnchorCells()
{
	SnapCandidateCache.Reset();
	AnchorCells.Reset();
	if (IBuildStructureHost* HostInterface = Host.GetInterface())
	{
		HostInterface->CollectAnchorCells(AnchorCells);
	}
}

bool UBuildStructureComponent::IsAnchored(const FBuildGridCoord& Coord) const
{
	if (const IBuildStructureHost* HostInterface = Host.GetInterface())
	{
		return HostInterface->IsCellAnchored(Coord);
	}
	return AnchorCells.Contains(Coord);
}

void UBuildStructureComponent::ForEachOccupiedKey(
	int32 EntryIndex,
	TFunctionRef<void(const FBuildSlotKey&)> Visitor) const
{
	if (!PieceList.Entries.IsValidIndex(EntryIndex))
	{
		return;
	}

	const FBuildPieceEntry& Entry = PieceList.Entries[EntryIndex];
	const UBuildPieceDefinition* Definition = PieceCatalog
		? PieceCatalog->GetByIndex(Entry.PieceIndex)
		: nullptr;
	const FIntPoint Footprint = Definition
		? FIntPoint(FMath::Max(1, Definition->Footprint.X), FMath::Max(1, Definition->Footprint.Y))
		: FIntPoint(1, 1);

	for (int32 DeltaX = 0; DeltaX < Footprint.X; ++DeltaX)
	{
		for (int32 DeltaY = 0; DeltaY < Footprint.Y; ++DeltaY)
		{
			Visitor(FBuildSlotKey(
				FBuildGridCoord(
					Entry.Key.Coord.X + DeltaX,
					Entry.Key.Coord.Y + DeltaY,
					Entry.Key.Coord.Level),
				Entry.Key.Slot,
				Entry.Key.EdgeIndex,
				Entry.Key.SubCell));
		}
	}
}

void UBuildStructureComponent::AddEntryToIndex(int32 EntryIndex)
{
	ForEachOccupiedKey(EntryIndex, [this, EntryIndex](const FBuildSlotKey& Key)
	{
		SlotToEntryIndex.Add(Key, EntryIndex);
	});
}

void UBuildStructureComponent::RemoveEntryKeysFromIndex(int32 EntryIndex)
{
	ForEachOccupiedKey(EntryIndex, [this](const FBuildSlotKey& Key)
	{
		SlotToEntryIndex.Remove(Key);
	});
}

void UBuildStructureComponent::RebuildIndex()
{
	SlotToEntryIndex.Reset();
	SlotToEntryIndex.Reserve(PieceList.Entries.Num());

	for (int32 EntryIndex = 0; EntryIndex < PieceList.Entries.Num(); ++EntryIndex)
	{
		const FBuildPieceEntry& Entry = PieceList.Entries[EntryIndex];
		const UBuildPieceDefinition* Definition = PieceCatalog
			? PieceCatalog->GetByIndex(Entry.PieceIndex)
			: nullptr;
		const FIntPoint Footprint = Definition
			? FIntPoint(FMath::Max(1, Definition->Footprint.X), FMath::Max(1, Definition->Footprint.Y))
			: FIntPoint(1, 1);

		for (int32 DeltaX = 0; DeltaX < Footprint.X; ++DeltaX)
		{
			for (int32 DeltaY = 0; DeltaY < Footprint.Y; ++DeltaY)
			{
				const FBuildGridCoord OccupiedCoord(
					Entry.Key.Coord.X + DeltaX,
					Entry.Key.Coord.Y + DeltaY,
					Entry.Key.Coord.Level);
				SlotToEntryIndex.Add(
					FBuildSlotKey(
						OccupiedCoord,
						Entry.Key.Slot,
						Entry.Key.EdgeIndex,
						Entry.Key.SubCell),
					EntryIndex);
			}
		}
	}
}

bool UBuildStructureComponent::CanPlacePiece(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition,
	FGameplayTag& OutFailReason) const
{
	OutFailReason = FGameplayTag();

	if (!Host || !Definition || !PieceCatalog || Definition->SlotType != Key.Slot)
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		return false;
	}

	uint16 UnusedPieceIndex = 0;
	if (!PieceCatalog->FindIndex(Definition, UnusedPieceIndex))
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		return false;
	}

	if (PieceList.Entries.Num() >= MaxPieceCount)
	{
		OutFailReason = BuildGameplayTags::Fail_LimitReached;
		return false;
	}

	const int32 FootprintX = FMath::Max(1, Definition->Footprint.X);
	const int32 FootprintY = FMath::Max(1, Definition->Footprint.Y);
	for (int32 DeltaX = 0; DeltaX < FootprintX; ++DeltaX)
	{
		for (int32 DeltaY = 0; DeltaY < FootprintY; ++DeltaY)
		{
			const FBuildGridCoord Coord(
				Key.Coord.X + DeltaX,
				Key.Coord.Y + DeltaY,
				Key.Coord.Level);
			const FBuildSlotKey SubKey(Coord, Key.Slot, Key.EdgeIndex, Key.SubCell);
			if (SlotToEntryIndex.Contains(SubKey)
				|| (Key.Slot == EBuildSlotType::Foundation && IsAnchored(Coord)))
			{
				OutFailReason = BuildGameplayTags::Fail_Occupied;
				return false;
			}
		}
	}

	if (!CheckSupport(Key, Definition))
	{
		OutFailReason = BuildGameplayTags::Fail_NoSupport;
		return false;
	}

	const UBuildPieceFragment_PlacementRules* PlacementRules =
		Definition->FindFragment<UBuildPieceFragment_PlacementRules>();
	if ((!PlacementRules || PlacementRules->bBlockedByPawns)
		&& Key.Slot != EBuildSlotType::Floor
		&& Key.Slot != EBuildSlotType::Foundation
		&& IsPlacementBlockedByPawn(Key, Definition))
	{
		OutFailReason = BuildGameplayTags::Fail_Blocked;
		return false;
	}

	if (const IBuildResourceSource* Source = ResourceSource.GetInterface())
	{
		if (!Source->HasResources(Definition->Costs))
		{
			OutFailReason = BuildGameplayTags::Fail_NoResource;
			return false;
		}
	}

	return true;
}

const UBuildPieceDefinition* UBuildStructureComponent::QueryPiece(const FBuildSlotKey& Key) const
{
	const int32* EntryIndex = SlotToEntryIndex.Find(Key);
	if (!EntryIndex || !PieceCatalog || !PieceList.Entries.IsValidIndex(*EntryIndex))
	{
		return nullptr;
	}

	return PieceCatalog->GetByIndex(PieceList.Entries[*EntryIndex].PieceIndex);
}

FBuildSlotKey UBuildStructureComponent::WorldToSlot(
	const FVector& WorldLocation,
	EBuildSlotType Slot) const
{
	const IBuildStructureHost* HostInterface = Host.GetInterface();
	if (!HostInterface)
	{
		return FBuildSlotKey();
	}

	const FVector LocalLocation =
		HostInterface->GetStructureSpace().InverseTransformPosition(WorldLocation);
	FBuildSlotKey Key(BuildGrid::LocalToCoord(LocalLocation, GridSettings), Slot);
	const FVector Center = BuildGrid::CoordToLocalCenter(Key.Coord, GridSettings);
	const FVector Delta = LocalLocation - Center;

	if (FBuildSlotKey::UsesEdgeIndex(Slot))
	{
		Key.EdgeIndex = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y)
			? (Delta.X > 0.0 ? 0 : 2)
			: (Delta.Y > 0.0 ? 1 : 3);
	}
	else if (FBuildSlotKey::UsesSubCell(Slot))
	{
		Key.SubCell = BuildGrid::FindNearestSubCell(Delta, GridSettings);
	}
	return Key;
}

FVector UBuildStructureComponent::SlotToWorld(const FBuildSlotKey& Key) const
{
	FVector LocalLocation = BuildGrid::CoordToLocalCenter(Key.Coord, GridSettings);
	if (FBuildSlotKey::UsesEdgeIndex(Key.Slot))
	{
		LocalLocation += BuildGrid::EdgeOffset(Key.EdgeIndex, GridSettings);
	}
	else if (FBuildSlotKey::UsesSubCell(Key.Slot))
	{
		LocalLocation += BuildGrid::SubCellOffset(Key.SubCell, GridSettings);
	}

	const IBuildStructureHost* HostInterface = Host.GetInterface();
	return HostInterface
		? HostInterface->GetStructureSpace().TransformPosition(LocalLocation)
		: LocalLocation;
}

FTransform UBuildStructureComponent::GetPieceRelativeTransform(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition,
	uint8 Rotation) const
{
	FVector LocalLocation = BuildGrid::CoordToLocalCenter(Key.Coord, GridSettings);
	float Yaw = 90.0f * (Rotation & 3);
	if (FBuildSlotKey::UsesEdgeIndex(Key.Slot))
	{
		// Walls and their decorations face out along their edge; the authored rotation is
		// ignored because the edge already determines the orientation.
		LocalLocation += BuildGrid::EdgeOffset(Key.EdgeIndex, GridSettings);
		Yaw = BuildGrid::EdgeYaw(Key.EdgeIndex);
	}
	else if (FBuildSlotKey::UsesSubCell(Key.Slot))
	{
		LocalLocation += BuildGrid::SubCellOffset(Key.SubCell, GridSettings);
	}

	if (Definition)
	{
		LocalLocation += Definition->MeshOffset;
	}

	return FTransform(
		FRotator(0.0f, Yaw, 0.0f),
		LocalLocation,
		Definition ? Definition->MeshScale : FVector::OneVector);
}

bool UBuildStructureComponent::CheckSupport(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition) const
{
	if (const UBuildPieceFragment_PlacementRules* Rules =
		Definition->FindFragment<UBuildPieceFragment_PlacementRules>())
	{
		if (Rules->bAllowFloating)
		{
			return true;
		}
	}

	const FBuildGridCoord& Coord = Key.Coord;
	const FBuildGridCoord CardinalNeighbors[4] = {
		{Coord.X + 1, Coord.Y, Coord.Level},
		{Coord.X - 1, Coord.Y, Coord.Level},
		{Coord.X, Coord.Y + 1, Coord.Level},
		{Coord.X, Coord.Y - 1, Coord.Level}
	};

	switch (Key.Slot)
	{
	case EBuildSlotType::Foundation:
		if (Coord.Level != 0)
		{
			return false;
		}
		for (const FBuildGridCoord& Neighbor : CardinalNeighbors)
		{
			if (IsAnchored(Neighbor)
				|| HasPieceAt(Neighbor, EBuildSlotType::Foundation)
				|| HasPieceAt(Neighbor, EBuildSlotType::Floor))
			{
				return true;
			}
		}
		return false;

	case EBuildSlotType::Floor:
		if (Coord.Level == 0
			&& (IsAnchored(Coord) || HasPieceAt(Coord, EBuildSlotType::Foundation)))
		{
			return true;
		}
		if (Coord.Level > 0
			&& HasPieceAt(
				FBuildGridCoord(Coord.X, Coord.Y, Coord.Level - 1),
				EBuildSlotType::Floor))
		{
			return true;
		}
		for (const FBuildGridCoord& Neighbor : CardinalNeighbors)
		{
			if ((Coord.Level == 0 && IsAnchored(Neighbor))
				|| HasPieceAt(Neighbor, EBuildSlotType::Floor)
				|| HasPieceAt(Neighbor, EBuildSlotType::Foundation))
			{
				return true;
			}
		}
		return false;

	case EBuildSlotType::WallProp:
		// A decoration hangs on a wall, so that exact edge has to carry one.
		return SlotToEntryIndex.Contains(
			FBuildSlotKey(Coord, EBuildSlotType::Wall, Key.EdgeIndex));

	case EBuildSlotType::Wall:
	case EBuildSlotType::Prop:
	default:
		return HasPieceAt(Coord, EBuildSlotType::Floor)
			|| HasPieceAt(Coord, EBuildSlotType::Foundation)
			|| (Coord.Level == 0 && IsAnchored(Coord));
	}
}

bool UBuildStructureComponent::HasPieceAt(
	const FBuildGridCoord& Coord,
	EBuildSlotType Slot) const
{
	return SlotToEntryIndex.Contains(FBuildSlotKey(Coord, Slot));
}

bool UBuildStructureComponent::IsPlacementBlockedByPawn(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition) const
{
	const UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	const IBuildStructureHost* HostInterface = Host.GetInterface();
	if (!World || !OwnerActor || !HostInterface || !Definition)
	{
		return false;
	}

	const FTransform StructureSpace = HostInterface->GetStructureSpace();
	const FTransform PieceRelativeTransform = GetPieceRelativeTransform(Key, Definition, 0);
	const FTransform PieceWorldTransform = PieceRelativeTransform * StructureSpace;

	// A raft section can intentionally be one large grid cell with walls on its edges and a
	// Dense prop snap grid. Testing the whole cell makes any pawn anywhere on the deck
	// block every wall and prop. Use the authored mesh bounds at the snapped transform so only
	// a pawn intersecting the actual proposed piece rejects placement.
	FVector PieceExtent(25.0, 25.0, FMath::Max(25.0, GridSettings.LevelHeight * 0.25));
	if (Definition->Mesh)
	{
		const FVector MeshExtent = Definition->Mesh->GetBounds().BoxExtent;
		const FVector MeshScale = Definition->MeshScale.GetAbs();
		PieceExtent = MeshExtent * MeshScale;
		PieceExtent.X = FMath::Max(PieceExtent.X, 5.0);
		PieceExtent.Y = FMath::Max(PieceExtent.Y, 5.0);
		PieceExtent.Z = FMath::Max(PieceExtent.Z, 5.0);
	}

	const FCollisionShape Shape = FCollisionShape::MakeBox(PieceExtent);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BuildPlacementBlocked), false, OwnerActor);
	return World->OverlapAnyTestByChannel(
		PieceWorldTransform.GetLocation(),
		PieceWorldTransform.GetRotation(),
		ECC_Pawn,
		Shape,
		QueryParams);
}

bool UBuildStructureComponent::IsInstigatorWithinRange(
	const FBuildSlotKey& Key,
	const AController* Instigator) const
{
	if (MaxPlacementDistance <= 0.0 || !Instigator)
	{
		return true;
	}

	const APawn* Pawn = Instigator->GetPawn();
	return Pawn
		&& FVector::DistSquared(Pawn->GetActorLocation(), SlotToWorld(Key))
			<= FMath::Square(MaxPlacementDistance);
}

bool UBuildStructureComponent::TryPlacePiece(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition,
	uint8 Rotation,
	AController* Instigator)
{
	FGameplayTag UnusedFailReason;
	return TryPlacePieceWithReason(Key, Definition, Rotation, Instigator, UnusedFailReason);
}

bool UBuildStructureComponent::TryPlacePieceWithReason(
	const FBuildSlotKey& Key,
	const UBuildPieceDefinition* Definition,
	uint8 Rotation,
	AController* Instigator,
	FGameplayTag& OutFailReason)
{
	AActor* OwnerActor = GetOwner();
	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("[BuildPlacement] Server try owner=%s authority=%d instigator=%s pawn=%s key=%s slot=%d edge=%u sub_cell=%u definition=%s rotation=%u entries=%d"),
		*GetNameSafe(OwnerActor),
		OwnerActor && OwnerActor->HasAuthority(),
		*GetNameSafe(Instigator),
		*GetNameSafe(Instigator ? Instigator->GetPawn() : nullptr),
		*Key.Coord.ToString(),
		static_cast<int32>(Key.Slot),
		Key.EdgeIndex,
		Key.SubCell,
		*GetNameSafe(Definition),
		Rotation,
		PieceList.Entries.Num());
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		OutFailReason = BuildGameplayTags::Fail_Blocked;
		UE_LOG(
			LogBuildingCore,
			Warning,
			TEXT("[BuildPlacement] Server rejected stage=Authority owner=%s key=%s reason=%s"),
			*GetNameSafe(OwnerActor),
			*Key.Coord.ToString(),
			*OutFailReason.ToString());
		return false;
	}

	if (!CanPlacePiece(Key, Definition, OutFailReason))
	{
		UE_LOG(
			LogBuildingCore,
			Warning,
			TEXT("[BuildPlacement] Server rejected stage=CanPlace owner=%s key=%s slot=%d edge=%u sub_cell=%u definition=%s reason=%s entries=%d max_entries=%d"),
			*GetNameSafe(OwnerActor),
			*Key.Coord.ToString(),
			static_cast<int32>(Key.Slot),
			Key.EdgeIndex,
			Key.SubCell,
			*GetNameSafe(Definition),
			*OutFailReason.ToString(),
			PieceList.Entries.Num(),
			MaxPieceCount);
		return false;
	}

	if (!IsInstigatorWithinRange(Key, Instigator))
	{
		OutFailReason = BuildGameplayTags::Fail_TooFar;
		const APawn* InstigatorPawn = Instigator ? Instigator->GetPawn() : nullptr;
		const double Distance = InstigatorPawn
			? FVector::Distance(InstigatorPawn->GetActorLocation(), SlotToWorld(Key))
			: -1.0;
		UE_LOG(
			LogBuildingCore,
			Warning,
			TEXT("[BuildPlacement] Server rejected stage=Range owner=%s instigator=%s pawn=%s key=%s distance=%.1f max_distance=%.1f reason=%s"),
			*GetNameSafe(OwnerActor),
			*GetNameSafe(Instigator),
			*GetNameSafe(InstigatorPawn),
			*Key.Coord.ToString(),
			Distance,
			MaxPlacementDistance,
			*OutFailReason.ToString());
		return false;
	}

	if (IBuildResourceSource* Source = ResourceSource.GetInterface())
	{
		if (!Source->ConsumeResources(Definition->Costs))
		{
			OutFailReason = BuildGameplayTags::Fail_NoResource;
			UE_LOG(
				LogBuildingCore,
				Warning,
				TEXT("[BuildPlacement] Server rejected stage=ConsumeResources owner=%s source=%s key=%s definition=%s reason=%s"),
				*GetNameSafe(OwnerActor),
				*GetNameSafe(ResourceSource.GetObject()),
				*Key.Coord.ToString(),
				*GetNameSafe(Definition),
				*OutFailReason.ToString());
			return false;
		}
	}

	uint16 PieceIndex = 0;
	if (!PieceCatalog->FindIndex(Definition, PieceIndex))
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		UE_LOG(
			LogBuildingCore,
			Error,
			TEXT("[BuildPlacement] Server rejected stage=CatalogIndex owner=%s catalog=%s key=%s definition=%s reason=%s"),
			*GetNameSafe(OwnerActor),
			*GetNameSafe(PieceCatalog),
			*Key.Coord.ToString(),
			*GetNameSafe(Definition),
			*OutFailReason.ToString());
		return false;
	}

	FBuildPieceEntry& NewEntry = PieceList.Entries.AddDefaulted_GetRef();
	NewEntry.Key = Key;
	NewEntry.PieceIndex = PieceIndex;
	NewEntry.Rotation = Rotation & 3;
	PieceList.MarkItemDirty(NewEntry);
	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("[BuildPlacement] Structure accepted host=%s definition=%s piece=%d key=%s slot=%d edge=%u sub_cell=%u rotation=%u entries=%d instigator=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Definition),
		PieceIndex,
		*Key.Coord.ToString(),
		static_cast<int32>(Key.Slot),
		Key.EdgeIndex,
		Key.SubCell,
		Rotation & 3,
		PieceList.Entries.Num(),
		*GetNameSafe(Instigator));

	// Only the new entry's cells enter the index; a full rebuild here is what made restoring
	// a save game quadratic.
	AddEntryToIndex(PieceList.Entries.Num() - 1);
	SnapCandidateCache.Reset();

	if (BatchEditDepth > 0)
	{
		bBatchDirty = true;
	}
	else
	{
		OwnerActor->FlushNetDormancy();
		OwnerActor->ForceNetUpdate();
		NotifyStructureChanged();
	}

	OutFailReason = FGameplayTag();
	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("[BuildPlacement] Server placed owner=%s key=%s slot=%d edge=%u sub_cell=%u piece=%u rotation=%u entries=%d instigator=%s"),
		*GetNameSafe(OwnerActor),
		*Key.Coord.ToString(),
		static_cast<int32>(Key.Slot),
		Key.EdgeIndex,
		Key.SubCell,
		PieceIndex,
		NewEntry.Rotation,
		PieceList.Entries.Num(),
		*GetNameSafe(Instigator));
	return true;
}

bool UBuildStructureComponent::TryRemovePiece(
	const FBuildSlotKey& Key,
	AController* Instigator)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	const int32* EntryIndex = SlotToEntryIndex.Find(Key);
	if (!EntryIndex || !PieceList.Entries.IsValidIndex(*EntryIndex))
	{
		return false;
	}

	const FBuildPieceEntry& Entry = PieceList.Entries[*EntryIndex];
	if (const UBuildPieceDefinition* Definition = PieceCatalog
		? PieceCatalog->GetByIndex(Entry.PieceIndex)
		: nullptr)
	{
		if (const UBuildPieceFragment_PlacementRules* Rules =
			Definition->FindFragment<UBuildPieceFragment_PlacementRules>())
		{
			if (!Rules->bRemovable)
			{
				return false;
			}
		}
	}

	if (!WouldStayConnectedWithout(*EntryIndex))
	{
		return false;
	}

	// Removing a floor takes its furniture and walls with it. Refusing instead would leave a
	// player unable to remodel a furnished room, which is the worse failure.
	TArray<FBuildSlotKey> DependentKeys;
	CollectDependentKeys(Key, DependentKeys);
	if (!DependentKeys.IsEmpty())
	{
		BeginBatchEdit();
		for (const FBuildSlotKey& DependentKey : DependentKeys)
		{
			TryRemovePiece(DependentKey, Instigator);
		}
		EndBatchEdit();

		// The dependents' removal reshuffled the entry array, so the original index is stale.
		EntryIndex = SlotToEntryIndex.Find(Key);
		if (!EntryIndex || !PieceList.Entries.IsValidIndex(*EntryIndex))
		{
			return false;
		}
	}

	// RemoveAtSwap moves the last entry into the hole, so only those two entries' keys change.
	const int32 RemovedIndex = *EntryIndex;
	const int32 LastIndex = PieceList.Entries.Num() - 1;
	RemoveEntryKeysFromIndex(RemovedIndex);
	if (RemovedIndex != LastIndex)
	{
		RemoveEntryKeysFromIndex(LastIndex);
	}

	PieceList.Entries.RemoveAtSwap(RemovedIndex);
	PieceList.MarkArrayDirty();

	if (PieceList.Entries.IsValidIndex(RemovedIndex))
	{
		AddEntryToIndex(RemovedIndex);
	}
	SnapCandidateCache.Reset();

	if (BatchEditDepth > 0)
	{
		bBatchDirty = true;
	}
	else
	{
		OwnerActor->FlushNetDormancy();
		OwnerActor->ForceNetUpdate();
		NotifyStructureChanged();
	}
	return true;
}

bool UBuildStructureComponent::WouldStayConnectedWithout(int32 SkipEntryIndex) const
{
	// Only load-bearing slots can disconnect anything; a wall or a chair never does, and this
	// path runs once per dependent when a furnished floor is removed.
	if (PieceList.Entries.IsValidIndex(SkipEntryIndex))
	{
		const EBuildSlotType SkipSlot = PieceList.Entries[SkipEntryIndex].Key.Slot;
		if (SkipSlot != EBuildSlotType::Foundation && SkipSlot != EBuildSlotType::Floor)
		{
			return true;
		}
	}

	if (const IBuildStructureHost* HostInterface = Host.GetInterface())
	{
		if (!HostInterface->RequiresConnectivity())
		{
			return true;
		}
	}

	TSet<FBuildGridCoord> SupportCells;
	for (int32 EntryIndex = 0; EntryIndex < PieceList.Entries.Num(); ++EntryIndex)
	{
		if (EntryIndex == SkipEntryIndex)
		{
			continue;
		}

		const FBuildSlotKey& Key = PieceList.Entries[EntryIndex].Key;
		if (Key.Slot == EBuildSlotType::Foundation || Key.Slot == EBuildSlotType::Floor)
		{
			SupportCells.Add(Key.Coord);
		}
	}

	TSet<FBuildGridCoord> Visited;
	TArray<FBuildGridCoord> Queue;
	for (const FBuildGridCoord& Anchor : AnchorCells)
	{
		Visited.Add(Anchor);
		Queue.Add(Anchor);
	}

	TArray<FBuildGridCoord> Neighbors;
	while (!Queue.IsEmpty())
	{
		const FBuildGridCoord Current = Queue.Pop(EAllowShrinking::No);
		BuildGrid::GetNeighbors(Current, Neighbors);
		for (const FBuildGridCoord& Neighbor : Neighbors)
		{
			if (SupportCells.Contains(Neighbor) && !Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Add(Neighbor);
			}
		}
	}

	for (int32 EntryIndex = 0; EntryIndex < PieceList.Entries.Num(); ++EntryIndex)
	{
		if (EntryIndex == SkipEntryIndex)
		{
			continue;
		}

		const FBuildGridCoord& Coord = PieceList.Entries[EntryIndex].Key.Coord;
		if (!Visited.Contains(Coord) && !IsAnchored(Coord))
		{
			return false;
		}
	}
	return true;
}

int32 UBuildStructureComponent::ClearAllPieces()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return 0;
	}

	const int32 RemovedCount = PieceList.Entries.Num();
	if (RemovedCount == 0)
	{
		return 0;
	}

	PieceList.Entries.Reset();
	PieceList.MarkArrayDirty();
	RebuildIndex();
	OwnerActor->FlushNetDormancy();
	OwnerActor->ForceNetUpdate();
	NotifyStructureChanged();
	return RemovedCount;
}

FBox UBuildStructureComponent::ComputePieceBounds() const
{
	FBox Bounds(ForceInit);
	const FVector2D CellSize = BuildGrid::GetCellSize(GridSettings);
	const FVector CellExtent(CellSize.X * 0.5, CellSize.Y * 0.5, 1.0);

	for (const FBuildPieceEntry& Entry : PieceList.Entries)
	{
		const UBuildPieceDefinition* Definition = PieceCatalog
			? PieceCatalog->GetByIndex(Entry.PieceIndex)
			: nullptr;
		const int32 FootprintX = Definition ? FMath::Max(1, Definition->Footprint.X) : 1;
		const int32 FootprintY = Definition ? FMath::Max(1, Definition->Footprint.Y) : 1;
		for (int32 DeltaX = 0; DeltaX < FootprintX; ++DeltaX)
		{
			for (int32 DeltaY = 0; DeltaY < FootprintY; ++DeltaY)
			{
				Bounds += FBox::BuildAABB(
					BuildGrid::CoordToLocalCenter(
						FBuildGridCoord(
							Entry.Key.Coord.X + DeltaX,
							Entry.Key.Coord.Y + DeltaY,
							Entry.Key.Coord.Level),
						GridSettings),
					CellExtent);
			}
		}
	}
	return Bounds;
}

void UBuildStructureComponent::CollectDependentKeys(
	const FBuildSlotKey& Key,
	TArray<FBuildSlotKey>& OutKeys) const
{
	OutKeys.Reset();

	auto AddIfPresent = [this, &OutKeys](const FBuildSlotKey& Candidate)
	{
		if (SlotToEntryIndex.Contains(Candidate))
		{
			OutKeys.AddUnique(Candidate);
		}
	};

	// A wall's decorations go with it, whether the wall is removed directly or as a dependent.
	auto AddWallDecoration = [&AddIfPresent](const FBuildGridCoord& Coord, uint8 EdgeIndex)
	{
		AddIfPresent(FBuildSlotKey(Coord, EBuildSlotType::WallProp, EdgeIndex));
	};

	switch (Key.Slot)
	{
	case EBuildSlotType::Foundation:
	case EBuildSlotType::Floor:
		// Everything standing on this cell, plus its walls and their decorations.
		for (uint8 SubCell = 0; SubCell < FBuildSlotKey::SubCellCount; ++SubCell)
		{
			AddIfPresent(FBuildSlotKey(Key.Coord, EBuildSlotType::Prop, 0, SubCell));
		}
		for (uint8 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
		{
			AddWallDecoration(Key.Coord, EdgeIndex);
			AddIfPresent(FBuildSlotKey(Key.Coord, EBuildSlotType::Wall, EdgeIndex));
		}
		break;

	case EBuildSlotType::Wall:
		AddWallDecoration(Key.Coord, Key.EdgeIndex);
		break;

	default:
		break;
	}
}

const TArray<FBuildGridCoord>& UBuildStructureComponent::GetSnapCandidates(
	EBuildSlotType Slot,
	int32 Level) const
{
	// The candidate ring only changes when the structure does, but the preview asks for it
	// every frame; recomputing it per frame is O(pieces) of hashing and allocation.
	const uint32 CacheKey = (static_cast<uint32>(Slot) << 24) ^ static_cast<uint32>(Level);
	if (const TArray<FBuildGridCoord>* Cached = SnapCandidateCache.Find(CacheKey))
	{
		return *Cached;
	}

	TArray<FBuildGridCoord>& Candidates = SnapCandidateCache.Add(CacheKey);
	CollectSnapCandidates(Slot, Level, Candidates);
	return Candidates;
}

void UBuildStructureComponent::CollectSnapCandidates(
	EBuildSlotType Slot,
	int32 Level,
	TArray<FBuildGridCoord>& OutCandidates) const
{
	OutCandidates.Reset();

	TSet<FBuildGridCoord> Occupied;
	if (Level == 0)
	{
		Occupied.Append(AnchorCells);
	}
	for (const FBuildPieceEntry& Entry : PieceList.Entries)
	{
		if (Entry.Key.Slot == Slot && Entry.Key.Coord.Level == Level)
		{
			Occupied.Add(Entry.Key.Coord);
		}
	}

	TSet<FBuildGridCoord> Seen;
	TArray<FBuildGridCoord> Neighbors;
	for (const FBuildGridCoord& Coord : Occupied)
	{
		BuildGrid::GetPlanarNeighbors(Coord, Neighbors);
		for (const FBuildGridCoord& Neighbor : Neighbors)
		{
			// An occupied cell is no longer a snap target: placing a piece disables its own cell.
			if (Occupied.Contains(Neighbor) || Seen.Contains(Neighbor))
			{
				continue;
			}
			Seen.Add(Neighbor);
			OutCandidates.Add(Neighbor);
		}
	}
}

bool UBuildStructureComponent::FindNearestSnapCandidate(
	const FVector& WorldLocation,
	EBuildSlotType Slot,
	int32 Level,
	FBuildSlotKey& OutKey) const
{
	const IBuildStructureHost* HostInterface = Host.GetInterface();
	if (!HostInterface)
	{
		return false;
	}

	const TArray<FBuildGridCoord>& Candidates = GetSnapCandidates(Slot, Level);
	if (Candidates.IsEmpty())
	{
		return false;
	}

	const FVector Local = HostInterface->GetStructureSpace().InverseTransformPosition(WorldLocation);
	const double MaxDistanceSquared = FMath::Square(
		BuildGrid::GetMaxCellSize(GridSettings) * MaxSnapCells);

	double BestDistanceSquared = MaxDistanceSquared;
	bool bFound = false;
	for (const FBuildGridCoord& Candidate : Candidates)
	{
		const FVector Center = BuildGrid::CoordToLocalCenter(Candidate, GridSettings);
		const double DistanceSquared =
			FMath::Square(Center.X - Local.X) + FMath::Square(Center.Y - Local.Y);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			OutKey = FBuildSlotKey(Candidate, Slot);
			bFound = true;
		}
	}
	return bFound;
}

FBox UBuildStructureComponent::ComputeLocalStructureBounds() const
{
	FBox Bounds(ForceInit);
	const FVector2D CellSize = BuildGrid::GetCellSize(GridSettings);
	const FVector CellExtent(CellSize.X * 0.5, CellSize.Y * 0.5, 1.0);
	for (const FBuildGridCoord& Anchor : AnchorCells)
	{
		Bounds += FBox::BuildAABB(BuildGrid::CoordToLocalCenter(Anchor, GridSettings), CellExtent);
	}

	for (const FBuildPieceEntry& Entry : PieceList.Entries)
	{
		const UBuildPieceDefinition* Definition = PieceCatalog
			? PieceCatalog->GetByIndex(Entry.PieceIndex)
			: nullptr;
		const int32 FootprintX = Definition ? FMath::Max(1, Definition->Footprint.X) : 1;
		const int32 FootprintY = Definition ? FMath::Max(1, Definition->Footprint.Y) : 1;
		for (int32 DeltaX = 0; DeltaX < FootprintX; ++DeltaX)
		{
			for (int32 DeltaY = 0; DeltaY < FootprintY; ++DeltaY)
			{
				Bounds += FBox::BuildAABB(
					BuildGrid::CoordToLocalCenter(
						FBuildGridCoord(
							Entry.Key.Coord.X + DeltaX,
							Entry.Key.Coord.Y + DeltaY,
							Entry.Key.Coord.Level),
						GridSettings),
					CellExtent);
			}
		}
	}
	return Bounds;
}

void UBuildStructureComponent::BeginBatchEdit()
{
	++BatchEditDepth;
}

void UBuildStructureComponent::EndBatchEdit()
{
	if (--BatchEditDepth > 0)
	{
		return;
	}

	BatchEditDepth = 0;
	if (!bBatchDirty)
	{
		return;
	}

	bBatchDirty = false;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->FlushNetDormancy();
		OwnerActor->ForceNetUpdate();
	}
	NotifyStructureChanged();
}

void UBuildStructureComponent::NotifyStructureChanged()
{
	// Any structure change invalidates every cached candidate ring.
	SnapCandidateCache.Reset();

	OnStructureChanged.Broadcast();
	if (IBuildStructureHost* HostInterface = Host.GetInterface())
	{
		// Hosts update local collision on every peer. Authority-only side effects such as
		// buoyancy remain the host implementation's responsibility.
		// Pieces only: the host owns its base size and must not grow from an empty structure.
		HostInterface->OnStructureBoundsChanged(ComputePieceBounds());
	}
}

void UBuildStructureComponent::SetResourceSource(UObject* NewResourceSource)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	IBuildResourceSource* SourceInterface = NewResourceSource
		? Cast<IBuildResourceSource>(NewResourceSource)
		: nullptr;
	ResourceSource.SetObject(SourceInterface ? NewResourceSource : nullptr);
	ResourceSource.SetInterface(SourceInterface);
}

void UBuildStructureComponent::SetPieceCatalog(UBuildPieceCatalog* NewCatalog)
{
	if (PieceCatalog == NewCatalog)
	{
		return;
	}

	PieceCatalog = NewCatalog;
	RebuildIndex();
	if (HasBegunPlay())
	{
		NotifyStructureChanged();
	}
}

void UBuildStructureComponent::SetDebugDrawingEnabled(bool bEnabled)
{
#if UE_WITH_CHEAT_MANAGER
	bDrawDebug = bEnabled;
	SetComponentTickEnabled(bDrawDebug);
#endif
}

void UBuildStructureComponent::DrawDebugStructure() const
{
#if UE_WITH_CHEAT_MANAGER
	const UWorld* World = GetWorld();
	const IBuildStructureHost* HostInterface = Host.GetInterface();
	if (!World || !HostInterface)
	{
		return;
	}

	const FTransform StructureSpace = HostInterface->GetStructureSpace();
	const FVector2D CellSize = BuildGrid::GetCellSize(GridSettings);
	const FVector CellExtent(CellSize.X * 0.5, CellSize.Y * 0.5, 5.0);
	for (const FBuildGridCoord& Anchor : AnchorCells)
	{
		DrawDebugBox(
			World,
			StructureSpace.TransformPosition(BuildGrid::CoordToLocalCenter(Anchor, GridSettings)),
			CellExtent,
			StructureSpace.GetRotation(),
			FColor::White,
			false,
			0.0f,
			0,
			1.0f);
	}
	for (const FBuildPieceEntry& Entry : PieceList.Entries)
	{
		DrawDebugBox(
			World,
			SlotToWorld(Entry.Key),
			CellExtent,
			StructureSpace.GetRotation(),
			FColor::Green,
			false,
			0.0f,
			0,
			2.0f);
	}

	// Free cells touching the structure: these are the snap targets, and an occupied cell
	// disappears from here automatically.
	for (const FBuildGridCoord& Candidate : GetSnapCandidates(EBuildSlotType::Floor, 0))
	{
		DrawDebugBox(
			World,
			StructureSpace.TransformPosition(BuildGrid::CoordToLocalCenter(Candidate, GridSettings)),
			CellExtent,
			StructureSpace.GetRotation(),
			FColor::Cyan,
			false,
			0.0f,
			0,
			1.5f);
	}

	const FBox Bounds = ComputeLocalStructureBounds();
	if (Bounds.IsValid)
	{
		DrawDebugBox(
			World,
			StructureSpace.TransformPosition(Bounds.GetCenter()),
			Bounds.GetExtent(),
			StructureSpace.GetRotation(),
			FColor::Red,
			false,
			0.0f,
			0,
			2.0f);
	}
#endif
}
