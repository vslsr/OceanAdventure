// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildCheats.h"

#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildPreviewComponent.h"
#include "Building/BuildStructureComponent.h"
#include "BuildingCoreRuntimeModule.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CheatManagerDefines.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildCheats)

namespace
{
	constexpr int32 BuildCoordNotSpecified = -2147483647;
}

UBuildCheats::UBuildCheats()
{
#if UE_WITH_CHEAT_MANAGER
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UCheatManager::RegisterForOnCheatManagerCreated(
			FOnCheatManagerCreated::FDelegate::CreateLambda([](UCheatManager* CheatManager)
			{
				CheatManager->AddCheatManagerExtension(NewObject<ThisClass>(CheatManager));
			}));
	}
#endif
}

void UBuildCheats::BuildMode(int32 bEnabled)
{
#if UE_WITH_CHEAT_MANAGER
	UBuildPreviewComponent* Preview = FindOrAddPreviewComponent(bEnabled != 0);
	if (!Preview)
	{
		UE_LOG(LogBuildingCore, Warning, TEXT("BuildMode: no controlled pawn or preview component"));
		return;
	}

	Preview->SetPreviewEnabled(bEnabled != 0);
	if (const UBuildStructureComponent* Structure = GetBuildComponent())
	{
		if (const UBuildPieceCatalog* Catalog = Structure->GetCatalog())
		{
			if (const UBuildPieceDefinition* Definition = GetSelectedPiece())
			{
				uint16 PieceIndex = 0;
				if (Catalog->FindIndex(Definition, PieceIndex))
				{
					Preview->SetSelectedPieceIndex(PieceIndex);
				}
			}
		}
	}

	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("Build ghost %s. The playable entry point is the build-mode ability (InputTag.Build.Mode); "
			"this cheat only toggles the local preview."),
		bEnabled != 0 ? TEXT("enabled") : TEXT("disabled"));
#endif
}

void UBuildCheats::BuildCreative(int32 bEnabled)
{
	BuildMode(bEnabled);
}

void UBuildCheats::BuildSelect(const FString& PieceTag)
{
#if UE_WITH_CHEAT_MANAGER
	const UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceCatalog* Catalog = Structure ? Structure->GetCatalog() : nullptr;
	const FGameplayTag RequestedTag = FGameplayTag::RequestGameplayTag(FName(*PieceTag), false);
	const UBuildPieceDefinition* Definition = Catalog ? Catalog->FindByTag(RequestedTag) : nullptr;
	if (!Definition)
	{
		UE_LOG(LogBuildingCore, Warning, TEXT("BuildSelect: unknown piece '%s'"), *PieceTag);
		return;
	}

	SelectedPieceTag = PieceTag;
	uint16 PieceIndex = 0;
	if (Catalog->FindIndex(Definition, PieceIndex))
	{
		if (UBuildPreviewComponent* Preview = FindOrAddPreviewComponent(false))
		{
			Preview->SetSelectedPieceIndex(PieceIndex);
		}
	}
	UE_LOG(LogBuildingCore, Display, TEXT("Selected build piece %s"), *PieceTag);
#endif
}

void UBuildCheats::BuildPlace(int32 X, int32 Y, int32 Level)
{
#if UE_WITH_CHEAT_MANAGER
	UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceDefinition* Definition = GetSelectedPiece();
	if (!Structure || !Definition)
	{
		return;
	}

	FBuildSlotKey Key;
	if (X == BuildCoordNotSpecified)
	{
		if (!GetCursorSlot(Key))
		{
			return;
		}
	}
	else
	{
		Key = FBuildSlotKey(FBuildGridCoord(X, Y, Level), Definition->SlotType);
	}

	FGameplayTag FailReason;
	if (!Structure->TryPlacePieceWithReason(
		Key,
		Definition,
		0,
		GetPlayerController(),
		FailReason))
	{
		UE_LOG(
			LogBuildingCore,
			Warning,
			TEXT("BuildPlace %s failed: %s"),
			*Key.Coord.ToString(),
			*FailReason.ToString());
	}
#endif
}

void UBuildCheats::BuildRemove(int32 X, int32 Y, int32 Level)
{
#if UE_WITH_CHEAT_MANAGER
	UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceDefinition* Definition = GetSelectedPiece();
	if (!Structure || !Definition)
	{
		return;
	}

	FBuildSlotKey Key;
	if (X == BuildCoordNotSpecified)
	{
		if (!GetCursorSlot(Key))
		{
			return;
		}
	}
	else
	{
		Key = FBuildSlotKey(FBuildGridCoord(X, Y, Level), Definition->SlotType);
	}

	if (!Structure->TryRemovePiece(Key, GetPlayerController()))
	{
		UE_LOG(LogBuildingCore, Warning, TEXT("BuildRemove %s failed"), *Key.Coord.ToString());
	}
#endif
}

void UBuildCheats::BuildFill(int32 SizeX, int32 SizeY)
{
#if UE_WITH_CHEAT_MANAGER
	UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceDefinition* Definition = GetSelectedPiece();
	if (!Structure || !Definition || Structure->GetAnchorCells().IsEmpty())
	{
		return;
	}

	SizeX = FMath::Clamp(SizeX, 1, 50);
	SizeY = FMath::Clamp(SizeY, 1, 50);
	int32 AnchorMaxX = MIN_int32;
	int32 AnchorMinY = MAX_int32;
	for (const FBuildGridCoord& Anchor : Structure->GetAnchorCells())
	{
		AnchorMaxX = FMath::Max(AnchorMaxX, Anchor.X);
		AnchorMinY = FMath::Min(AnchorMinY, Anchor.Y);
	}

	// One index, ISM and net update for the whole fill instead of one per piece.
	Structure->BeginBatchEdit();
	int32 PlacedCount = 0;
	for (int32 DeltaY = 0; DeltaY < SizeY; ++DeltaY)
	{
		for (int32 DeltaX = 0; DeltaX < SizeX; ++DeltaX)
		{
			const FBuildSlotKey Key(
				FBuildGridCoord(AnchorMaxX + 1 + DeltaX, AnchorMinY + DeltaY, 0),
				Definition->SlotType);
			if (Structure->TryPlacePiece(Key, Definition, 0, GetPlayerController()))
			{
				++PlacedCount;
			}
		}
	}
	Structure->EndBatchEdit();
	UE_LOG(LogBuildingCore, Display, TEXT("BuildFill placed %d pieces"), PlacedCount);
#endif
}

void UBuildCheats::BuildClear()
{
#if UE_WITH_CHEAT_MANAGER
	if (UBuildStructureComponent* Structure = GetBuildComponent())
	{
		UE_LOG(
			LogBuildingCore,
			Display,
			TEXT("BuildClear removed %d pieces"),
			Structure->ClearAllPieces());
	}
#endif
}

void UBuildCheats::BuildDump()
{
#if UE_WITH_CHEAT_MANAGER
	const UBuildStructureComponent* Structure = GetBuildComponent();
	if (!Structure)
	{
		return;
	}

	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("Build structure: %d anchors, %d pieces"),
		Structure->GetAnchorCells().Num(),
		Structure->GetEntries().Num());
	for (const FBuildPieceEntry& Entry : Structure->GetEntries())
	{
		const UBuildPieceDefinition* Definition = Structure->GetCatalog()
			? Structure->GetCatalog()->GetByIndex(Entry.PieceIndex)
			: nullptr;
		UE_LOG(
			LogBuildingCore,
			Display,
			TEXT("  %s slot=%d edge=%d rot=%d piece=%s"),
			*Entry.Key.Coord.ToString(),
			static_cast<int32>(Entry.Key.Slot),
			Entry.Key.EdgeIndex,
			Entry.Rotation,
			Definition ? *Definition->PieceTag.ToString() : TEXT("<invalid>"));
	}
#endif
}

void UBuildCheats::BuildDebug(int32 bEnabled)
{
#if UE_WITH_CHEAT_MANAGER
	if (UBuildStructureComponent* Structure = GetBuildComponent())
	{
		Structure->SetDebugDrawingEnabled(bEnabled != 0);
	}
#endif
}

UBuildStructureComponent* UBuildCheats::GetBuildComponent() const
{
#if UE_WITH_CHEAT_MANAGER
	APlayerController* PlayerController = GetPlayerController();
	if (!PlayerController)
	{
		return nullptr;
	}

	const APawn* Pawn = PlayerController->GetPawn();
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UPrimitiveComponent* MovementBase = Character->GetMovementBase())
		{
			if (AActor* BaseOwner = MovementBase->GetOwner())
			{
				if (UBuildStructureComponent* Structure =
					BaseOwner->FindComponentByClass<UBuildStructureComponent>())
				{
					return Structure;
				}
			}
		}
	}

	UBuildStructureComponent* BestStructure = nullptr;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	const FVector SearchOrigin = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	for (TObjectIterator<UBuildStructureComponent> It; It; ++It)
	{
		UBuildStructureComponent* Candidate = *It;
		if (!IsValid(Candidate)
			|| Candidate->HasAnyFlags(RF_ClassDefaultObject)
			|| Candidate->GetWorld() != PlayerController->GetWorld()
			|| !Candidate->GetOwner())
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(
			SearchOrigin,
			Candidate->GetOwner()->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestStructure = Candidate;
		}
	}
	return BestStructure;
#else
	return nullptr;
#endif
}

UBuildPreviewComponent* UBuildCheats::FindOrAddPreviewComponent(bool bCreateIfMissing) const
{
#if UE_WITH_CHEAT_MANAGER
	APlayerController* PlayerController = GetPlayerController();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return nullptr;
	}

	if (UBuildPreviewComponent* Existing = Pawn->FindComponentByClass<UBuildPreviewComponent>())
	{
		return Existing;
	}
	if (!bCreateIfMissing)
	{
		return nullptr;
	}

	// The preview is local-only now, so the cheat can create one on a client as well.
	UBuildPreviewComponent* Created = NewObject<UBuildPreviewComponent>(
		Pawn,
		UBuildPreviewComponent::StaticClass(),
		TEXT("BuildPreviewComponent"),
		RF_Transient);
	Pawn->AddInstanceComponent(Created);
	Created->RegisterComponent();
	Pawn->ForceNetUpdate();
	return Created;
#else
	return nullptr;
#endif
}

const UBuildPieceDefinition* UBuildCheats::GetSelectedPiece() const
{
#if UE_WITH_CHEAT_MANAGER
	const UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceCatalog* Catalog = Structure ? Structure->GetCatalog() : nullptr;
	if (!Catalog)
	{
		return nullptr;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*SelectedPieceTag), false);
	if (const UBuildPieceDefinition* Selected = Catalog->FindByTag(Tag))
	{
		return Selected;
	}
	return Catalog->GetByIndex(0);
#else
	return nullptr;
#endif
}

bool UBuildCheats::GetCursorSlot(FBuildSlotKey& OutKey) const
{
#if UE_WITH_CHEAT_MANAGER
	APlayerController* PlayerController = GetPlayerController();
	UBuildStructureComponent* Structure = GetBuildComponent();
	const UBuildPieceDefinition* Definition = GetSelectedPiece();
	if (!PlayerController || !Structure || !Definition)
	{
		return false;
	}

	FHitResult Hit;
	if (PlayerController->GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		Hit)
		&& Hit.bBlockingHit)
	{
		OutKey = Structure->WorldToSlot(Hit.ImpactPoint, Definition->SlotType);
		return true;
	}
#endif
	return false;
}
