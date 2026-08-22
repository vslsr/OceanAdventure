// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildPreviewComponent.h"

#include "Building/BuildGameplayTags.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureHost.h"
#include "Building/BuildStructureSubsystem.h"
#include "BuildingCoreRuntimeModule.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildPreviewComponent)

namespace
{
	const FName ShakeAmplitudeParameter(TEXT("ShakeAmplitude"));
}

UBuildPreviewComponent::UBuildPreviewComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// Purely local presentation: nothing here is replicated.
	SetIsReplicatedByDefault(false);
}

void UBuildPreviewComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(false);
}

void UBuildPreviewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyPreviewMesh();
	Super::EndPlay(EndPlayReason);
}

void UBuildPreviewComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPreviewEnabled && GetLocalPlayerController())
	{
		UpdatePreview();
	}
}

void UBuildPreviewComponent::SetPreviewEnabled(bool bEnabled)
{
	if (bPreviewEnabled == bEnabled)
	{
		return;
	}

	bPreviewEnabled = bEnabled;
	NextHostSearchSeconds = 0.0;
	bHasLoggedPreviewState = false;
	UE_LOG(
		LogBuildingCore,
		Display,
		TEXT("[BuildPlacement] Preview enabled=%d owner=%s local_controller=%s selected_piece=%d"),
		bPreviewEnabled,
		*GetNameSafe(GetOwner()),
		*GetNameSafe(GetLocalPlayerController()),
		SelectedPieceIndex);
	if (bPreviewEnabled && GetLocalPlayerController())
	{
		SetComponentTickEnabled(true);
		UpdatePreview();
	}
	else
	{
		SetComponentTickEnabled(false);
		DestroyPreviewMesh();
		CurrentStructure.Reset();
		bCurrentPlacementValid = false;
	}
}

void UBuildPreviewComponent::SetSelectedPieceIndex(int32 NewPieceIndex)
{
	const int32 ClampedIndex = FMath::Max(0, NewPieceIndex);
	if (SelectedPieceIndex == ClampedIndex)
	{
		return;
	}

	SelectedPieceIndex = ClampedIndex;
	if (PreviewMesh)
	{
		PreviewMesh->SetStaticMesh(nullptr);
	}
	ConfiguredDefinition.Reset();
	InvalidPreviewMaterialInstances.Reset();
	bUsingInvalidPreviewMaterial = false;
	bHasAppliedMaterialState = false;
	bHasCurrentKey = false;
}

void UBuildPreviewComponent::CycleSelectedPiece(int32 Delta)
{
	const UBuildStructureComponent* Structure = CurrentStructure.Get();
	const UBuildPieceCatalog* Catalog = Structure ? Structure->GetCatalog() : nullptr;
	const int32 PieceCount = Catalog ? Catalog->Pieces.Num() : 0;
	if (PieceCount <= 0)
	{
		return;
	}

	const int32 NextIndex = ((SelectedPieceIndex + Delta) % PieceCount + PieceCount) % PieceCount;
	SetSelectedPieceIndex(NextIndex);
}

void UBuildPreviewComponent::TriggerFailureFeedback(FGameplayTag FailReason)
{
	UE_LOG(
		LogBuildingCore,
		Warning,
		TEXT("[BuildPlacement] Preview failure feedback owner=%s host=%s key=%s slot=%d edge=%u sub_cell=%u valid=%d reason=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(CurrentStructure.Get()),
		bHasCurrentKey ? *CurrentKey.Coord.ToString() : TEXT("<none>"),
		static_cast<int32>(CurrentKey.Slot),
		CurrentKey.EdgeIndex,
		CurrentKey.SubCell,
		bCurrentPlacementValid,
		*FailReason.ToString());
	if (const UWorld* World = GetWorld())
	{
		FailureShakeEndSeconds = World->GetTimeSeconds() + FailureShakeDuration;
	}
	CurrentFailReason = FailReason;
}

APlayerController* UBuildPreviewComponent::GetLocalPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = OwnerPawn
		? Cast<APlayerController>(OwnerPawn->GetController())
		: nullptr;
	return PlayerController && PlayerController->IsLocalController() ? PlayerController : nullptr;
}

UBuildStructureComponent* UBuildPreviewComponent::FindBuildStructure(
	APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return nullptr;
	}

	// 1) The host the pawn is standing on wins: that is the raft the player is working from.
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

	// Everything below costs a scene query or a registry walk, so it runs on an interval and
	// the last answer is reused in between.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now < NextHostSearchSeconds)
	{
		if (UBuildStructureComponent* Cached = CurrentStructure.Get())
		{
			return Cached;
		}
	}
	NextHostSearchSeconds = Now + FMath::Max(0.0, HostSearchInterval);

	// 2) Otherwise whatever is under the cursor.
	FHitResult CursorHit;
	if (PlayerController->GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		CursorHit))
	{
		if (AActor* HitActor = CursorHit.GetActor())
		{
			if (UBuildStructureComponent* Structure =
				HitActor->FindComponentByClass<UBuildStructureComponent>())
			{
				return Structure;
			}
		}
	}

	// 3) Fall back to the registry, which holds only the live hosts.
	UBuildStructureSubsystem* Registry = World ? World->GetSubsystem<UBuildStructureSubsystem>() : nullptr;
	return Registry
		? Registry->FindNearestStructure(
			Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector,
			HostSearchRadius)
		: nullptr;
}

bool UBuildPreviewComponent::ProjectCursorToStructure(
	APlayerController* PlayerController,
	const UBuildStructureComponent* Structure,
	FVector& OutWorldLocation) const
{
	const IBuildStructureHost* Host = Structure ? Structure->GetHost() : nullptr;
	if (!PlayerController || !Host)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	const FTransform StructureSpace = Host->GetStructureSpace();
	// Level 0's walking surface, not the actor origin: the deck top is where pieces live.
	const FVector PlaneOrigin = StructureSpace.TransformPosition(
		FVector(0.0, 0.0, Host->GetGridSettings().BaseHeight));
	const FVector PlaneNormal = StructureSpace.GetUnitAxis(EAxis::Z);
	const double Denominator = FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::IsNearlyZero(Denominator))
	{
		return false;
	}

	const double RayDistance = FVector::DotProduct(PlaneOrigin - RayOrigin, PlaneNormal) / Denominator;
	if (RayDistance < 0.0)
	{
		return false;
	}

	OutWorldLocation = RayOrigin + RayDirection * RayDistance;
	return true;
}

void UBuildPreviewComponent::DestroyPreviewMesh()
{
	if (PreviewMesh)
	{
		PreviewMesh->DestroyComponent();
		PreviewMesh = nullptr;
	}
	ConfiguredDefinition.Reset();
	InvalidPreviewMaterialInstances.Reset();
	bUsingInvalidPreviewMaterial = false;
	bHasAppliedMaterialState = false;
	bHasCurrentKey = false;
}

void UBuildPreviewComponent::HidePreview()
{
	// Destroying and re-registering a component every frame rebuilds its render state and
	// reads as flicker; the failure paths only need to stop drawing.
	if (PreviewMesh)
	{
		PreviewMesh->SetVisibility(false, true);
	}
	bHasCurrentKey = false;
}

void UBuildPreviewComponent::ConfigurePreviewMesh(const UBuildPieceDefinition* Definition)
{
	if (!Definition || !Definition->Mesh)
	{
		DestroyPreviewMesh();
		return;
	}

	if (!PreviewMesh)
	{
		AActor* OwnerActor = GetOwner();
		if (!OwnerActor)
		{
			return;
		}
		PreviewMesh = NewObject<UStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
		PreviewMesh->SetMobility(EComponentMobility::Movable);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetGenerateOverlapEvents(false);
		PreviewMesh->SetCanEverAffectNavigation(false);
		PreviewMesh->SetCastShadow(false);
		PreviewMesh->SetTranslucentSortPriority(100);
		PreviewMesh->SetBoundsScale(2.0f);
		OwnerActor->AddInstanceComponent(PreviewMesh);
		PreviewMesh->RegisterComponent();
	}

	const bool bDefinitionChanged = ConfiguredDefinition.Get() != Definition;
	if (PreviewMesh->GetStaticMesh() != Definition->Mesh || bDefinitionChanged)
	{
		PreviewMesh->SetStaticMesh(Definition->Mesh);
		ConfiguredDefinition = Definition;
		InvalidPreviewMaterialInstances.Reset();
		bUsingInvalidPreviewMaterial = false;
		bHasAppliedMaterialState = false;
		for (int32 MaterialIndex = 0; MaterialIndex < Definition->OverrideMaterials.Num(); ++MaterialIndex)
		{
			if (UMaterialInterface* Material = Definition->OverrideMaterials[MaterialIndex])
			{
				PreviewMesh->SetMaterial(MaterialIndex, Material);
			}
		}
	}
}

void UBuildPreviewComponent::ApplyPreviewMaterial(
	const UBuildPieceDefinition* Definition,
	bool bPlacementValid,
	float ShakeAmplitude)
{
	if (!PreviewMesh || !Definition)
	{
		return;
	}

	const bool bWantInvalidMaterial = !bPlacementValid && Definition->InvalidPreviewMaterial != nullptr;
	const bool bStateChanged = !bHasAppliedMaterialState || bLastAppliedValid != bPlacementValid;

	if (bStateChanged)
	{
		// Swapping materials is only done on an actual state transition. Doing it every frame
		// while the valid/invalid result oscillates is what made the ghost strobe.
		if (bWantInvalidMaterial)
		{
			const int32 MaterialCount = FMath::Max(1, PreviewMesh->GetNumMaterials());
			InvalidPreviewMaterialInstances.Reset(MaterialCount);
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(
					Definition->InvalidPreviewMaterial,
					PreviewMesh);
				InvalidPreviewMaterialInstances.Add(MaterialInstance);
				PreviewMesh->SetMaterial(MaterialIndex, MaterialInstance);
			}
			bUsingInvalidPreviewMaterial = true;
		}
		else if (bUsingInvalidPreviewMaterial)
		{
			RestorePieceMaterials(Definition);
			InvalidPreviewMaterialInstances.Reset();
			bUsingInvalidPreviewMaterial = false;
		}

		bLastAppliedValid = bPlacementValid;
		bHasAppliedMaterialState = true;
	}

	if (bUsingInvalidPreviewMaterial)
	{
		for (UMaterialInstanceDynamic* MaterialInstance : InvalidPreviewMaterialInstances)
		{
			if (MaterialInstance)
			{
				MaterialInstance->SetScalarParameterValue(
					ShakeAmplitudeParameter,
					FMath::Max(0.0f, ShakeAmplitude));
			}
		}
	}
}

void UBuildPreviewComponent::RestorePieceMaterials(const UBuildPieceDefinition* Definition)
{
	if (!PreviewMesh || !Definition || !Definition->Mesh)
	{
		return;
	}

	const int32 MaterialCount = FMath::Max(1, PreviewMesh->GetNumMaterials());
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* Material = Definition->OverrideMaterials.IsValidIndex(MaterialIndex)
			? Definition->OverrideMaterials[MaterialIndex].Get()
			: Definition->Mesh->GetMaterial(MaterialIndex);
		PreviewMesh->SetMaterial(MaterialIndex, Material);
	}
}

void UBuildPreviewComponent::UpdatePreview()
{
	const TWeakObjectPtr<UBuildStructureComponent> PreviousStructure = CurrentStructure;
	const FBuildSlotKey PreviousKey = CurrentKey;
	const bool bPreviouslyHadKey = bHasCurrentKey;
	const bool bPreviouslyValid = bCurrentPlacementValid;
	const FGameplayTag PreviousFailReason = CurrentFailReason;
	const auto LogPreviewState = [this, PreviousStructure, PreviousKey, bPreviouslyHadKey,
		bPreviouslyValid, PreviousFailReason](const TCHAR* Stage, const UBuildPieceDefinition* Definition)
	{
		const bool bStateChanged = PreviousStructure.Get() != CurrentStructure.Get()
			|| bPreviouslyHadKey != bHasCurrentKey
			|| (bHasCurrentKey && !(PreviousKey == CurrentKey))
			|| bPreviouslyValid != bCurrentPlacementValid
			|| PreviousFailReason != CurrentFailReason;
		if (bHasLoggedPreviewState && !bStateChanged)
		{
			return;
		}

		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		const double Distance = OwnerPawn && CurrentStructure.IsValid() && bHasCurrentKey
			? FVector::Distance(OwnerPawn->GetActorLocation(), CurrentStructure->SlotToWorld(CurrentKey))
			: -1.0;
		UE_LOG(
			LogBuildingCore,
			Display,
			TEXT("[BuildPlacement] Preview state stage=%s owner=%s host=%s definition=%s piece=%d cursor=%s key=%s slot=%d edge=%u sub_cell=%u valid=%d reason=%s distance=%.1f local_limit=%.1f"),
			Stage,
			*GetNameSafe(GetOwner()),
			*GetNameSafe(CurrentStructure.Get()),
			*GetNameSafe(Definition),
			SelectedPieceIndex,
			*CurrentCursorWorld.ToCompactString(),
			bHasCurrentKey ? *CurrentKey.Coord.ToString() : TEXT("<none>"),
			static_cast<int32>(CurrentKey.Slot),
			CurrentKey.EdgeIndex,
			CurrentKey.SubCell,
			bCurrentPlacementValid,
			*CurrentFailReason.ToString(),
			Distance,
			LocalPlacementDistance);
		bHasLoggedPreviewState = true;
	};

	APlayerController* PlayerController = GetLocalPlayerController();
	UBuildStructureComponent* Structure = FindBuildStructure(PlayerController);
	CurrentStructure = Structure;
	if (!Structure)
	{
		HidePreview();
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_BadDefinition;
		LogPreviewState(TEXT("NoStructure"), nullptr);
		return;
	}

	const UBuildPieceCatalog* Catalog = Structure->GetCatalog();
	const UBuildPieceDefinition* Definition = Catalog
		? Catalog->GetByIndex(SelectedPieceIndex)
		: nullptr;
	if (!Definition || !Definition->Mesh)
	{
		HidePreview();
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_BadDefinition;
		LogPreviewState(TEXT("MissingDefinitionOrMesh"), Definition);
		return;
	}
	if (!ProjectCursorToStructure(PlayerController, Structure, CurrentCursorWorld))
	{
		HidePreview();
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_BadDefinition;
		LogPreviewState(TEXT("CursorProjectionFailed"), Definition);
		return;
	}

	ConfigurePreviewMesh(Definition);
	IBuildStructureHost* Host = Structure->GetHost();
	USceneComponent* AttachRoot = Host ? Host->GetStructureAttachRoot() : nullptr;
	if (!PreviewMesh || !AttachRoot)
	{
		UE_LOG(
			LogBuildingCore,
			Warning,
			TEXT("[BuildPlacement] Preview update aborted stage=MissingMeshOrAttachRoot owner=%s host=%s preview_mesh=%s attach_root=%s definition=%s"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Structure),
			*GetNameSafe(PreviewMesh),
			*GetNameSafe(AttachRoot),
			*GetNameSafe(Definition));
		return;
	}

	if (PreviewMesh->GetAttachParent() != AttachRoot)
	{
		PreviewMesh->AttachToComponent(
			AttachRoot,
			FAttachmentTransformRules::KeepRelativeTransform);
	}

	// Deck-like pieces snap to the ring of free cells around what already exists, so the four
	// sides of the raft are the only targets until the structure grows. Occupied cells drop out
	// of that set on their own, which is what disables a cell once it has been built on.
	const bool bUsesSnapRing = Definition->SlotType == EBuildSlotType::Foundation
		|| Definition->SlotType == EBuildSlotType::Floor;
	FBuildSlotKey CandidateKey;
	if (!bUsesSnapRing
		|| !Structure->FindNearestSnapCandidate(
			CurrentCursorWorld,
			Definition->SlotType,
			/*Level=*/0,
			CandidateKey))
	{
		CandidateKey = Structure->WorldToSlot(CurrentCursorWorld, Definition->SlotType);
	}

	// Hysteresis: hold the current cell while the cursor is still near its centre.
	if (bHasCurrentKey
		&& !(CandidateKey == CurrentKey)
		&& CandidateKey.Slot == CurrentKey.Slot)
	{
		const double HoldRadius = BuildGrid::GetMinCellSize(Structure->GetGridSettings())
			* SlotSwitchHysteresis;
		if (FVector::DistSquaredXY(CurrentCursorWorld, Structure->SlotToWorld(CurrentKey))
			< FMath::Square(HoldRadius))
		{
			CandidateKey = CurrentKey;
		}
	}
	CurrentKey = CandidateKey;
	bHasCurrentKey = true;

	bCurrentPlacementValid = Structure->CanPlacePiece(
		CurrentKey,
		Definition,
		CurrentFailReason);
	if (bCurrentPlacementValid && !IsLocallyWithinRange(Structure, CurrentKey))
	{
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_TooFar;
	}
	LogPreviewState(TEXT("Validated"), Definition);

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float ShakeAlpha = FailureShakeDuration > 0.0f
		? FMath::Clamp(
			static_cast<float>((FailureShakeEndSeconds - Now) / FailureShakeDuration),
			0.0f,
			1.0f)
		: 0.0f;
	// A rejected click is presentation state, not a new placement verdict. Force the invalid
	// material for the short feedback window even when the cursor is still over a locally valid
	// slot (for example a server-side race rejection); the next update restores the real state.
	const bool bShowFailureFeedback = ShakeAlpha > 0.0f;
	ApplyPreviewMaterial(
		Definition,
		bCurrentPlacementValid && !bShowFailureFeedback,
		FailureShakeAmplitude * ShakeAlpha);

	// The ghost always stays on the snapped cell. Letting it jump to the raw cursor position
	// while invalid made every validity flip look like a teleport.
	FTransform PreviewTransform = Structure->GetPieceRelativeTransform(CurrentKey, Definition, 0);
	PreviewTransform.AddToTranslation(FVector(0.0, 0.0, PreviewLiftZ));
	PreviewMesh->SetRelativeTransform(PreviewTransform);
	PreviewMesh->SetVisibility(true, true);
}

bool UBuildPreviewComponent::IsLocallyWithinRange(
	const UBuildStructureComponent* Structure,
	const FBuildSlotKey& Key) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn
		&& FVector::DistSquared(OwnerPawn->GetActorLocation(), Structure->SlotToWorld(Key))
			<= FMath::Square(LocalPlacementDistance);
}
