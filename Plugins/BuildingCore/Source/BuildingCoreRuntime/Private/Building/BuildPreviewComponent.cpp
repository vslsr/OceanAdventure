// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildPreviewComponent.h"

#include "Building/BuildGameplayTags.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureHost.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"

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
	SetIsReplicatedByDefault(true);
}

void UBuildPreviewComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshLocalMode();
}

void UBuildPreviewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PlayerController = GetLocalPlayerController())
	{
		if (bCapturedCursorState)
		{
			PlayerController->bShowMouseCursor = bPreviousShowMouseCursor;
		}
	}
	bCapturedCursorState = false;
	DestroyPreviewMesh();
	Super::EndPlay(EndPlayReason);
}

void UBuildPreviewComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PlayerController = GetLocalPlayerController();
	if (!bBuildModeEnabled || !PlayerController)
	{
		return;
	}

	UpdatePreview();

	if (PlayerController->WasInputKeyJustPressed(EKeys::Escape)
		|| PlayerController->WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		ServerSetBuildModeEnabled(false);
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		if (bCurrentPlacementValid && CurrentStructure.IsValid())
		{
			ServerTryPlace(CurrentStructure->GetOwner(), CurrentKey, 0);
		}
		else
		{
			TriggerFailureFeedback(CurrentFailReason);
		}
	}
}

void UBuildPreviewComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBuildPreviewComponent, bBuildModeEnabled, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBuildPreviewComponent, SelectedPieceIndex, COND_OwnerOnly);
}

void UBuildPreviewComponent::SetBuildModeEnabled(bool bEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || bBuildModeEnabled == bEnabled)
	{
		return;
	}

	bBuildModeEnabled = bEnabled;
	RefreshLocalMode();
	OwnerActor->ForceNetUpdate();
}

void UBuildPreviewComponent::SetSelectedPieceIndex(int32 NewPieceIndex)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	SelectedPieceIndex = static_cast<uint16>(FMath::Clamp(NewPieceIndex, 0, MAX_uint16));
	OnRep_SelectedPieceIndex();
	OwnerActor->ForceNetUpdate();
}

void UBuildPreviewComponent::OnRep_BuildModeEnabled()
{
	RefreshLocalMode();
}

void UBuildPreviewComponent::OnRep_SelectedPieceIndex()
{
	if (PreviewMesh)
	{
		PreviewMesh->SetStaticMesh(nullptr);
	}
	ConfiguredDefinition.Reset();
	InvalidPreviewMaterialInstances.Reset();
	bUsingInvalidPreviewMaterial = false;
}

void UBuildPreviewComponent::ServerSetBuildModeEnabled_Implementation(bool bEnabled)
{
	SetBuildModeEnabled(bEnabled);
}

void UBuildPreviewComponent::ServerTryPlace_Implementation(
	AActor* HostActor,
	FBuildSlotKey Key,
	uint8 Rotation)
{
	UBuildStructureComponent* Structure = HostActor
		? HostActor->FindComponentByClass<UBuildStructureComponent>()
		: nullptr;
	const UBuildPieceCatalog* Catalog = Structure ? Structure->GetCatalog() : nullptr;
	const UBuildPieceDefinition* Definition = Catalog
		? Catalog->GetByIndex(SelectedPieceIndex)
		: nullptr;
	AController* InstigatingController = nullptr;
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		InstigatingController = OwnerPawn->GetController();
	}

	FGameplayTag FailReason;
	if (!bBuildModeEnabled
		|| !Structure
		|| !Definition
		|| !Structure->TryPlacePieceWithReason(
			Key,
			Definition,
			Rotation,
			InstigatingController,
			FailReason))
	{
		if (!FailReason.IsValid())
		{
			FailReason = BuildGameplayTags::Fail_BadDefinition;
		}
		ClientPlacementRejected(FailReason);
	}
}

void UBuildPreviewComponent::ClientPlacementRejected_Implementation(FGameplayTag FailReason)
{
	TriggerFailureFeedback(FailReason);
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

	UBuildStructureComponent* BestStructure = nullptr;
	double BestDistanceSquared = FMath::Square(HostSearchRadius);
	const FVector SearchOrigin = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	for (TObjectIterator<UBuildStructureComponent> It; It; ++It)
	{
		UBuildStructureComponent* Candidate = *It;
		if (!IsValid(Candidate)
			|| Candidate->HasAnyFlags(RF_ClassDefaultObject)
			|| Candidate->GetWorld() != GetWorld()
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
	const FVector PlaneOrigin = StructureSpace.GetLocation();
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

void UBuildPreviewComponent::RefreshLocalMode()
{
	APlayerController* PlayerController = GetLocalPlayerController();
	if (!PlayerController)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (bBuildModeEnabled)
	{
		if (!bCapturedCursorState)
		{
			bPreviousShowMouseCursor = PlayerController->bShowMouseCursor;
			bCapturedCursorState = true;
		}
		PlayerController->bShowMouseCursor = true;
		SetComponentTickEnabled(true);
	}
	else
	{
		SetComponentTickEnabled(false);
		DestroyPreviewMesh();
		CurrentStructure.Reset();
		if (bCapturedCursorState)
		{
			PlayerController->bShowMouseCursor = bPreviousShowMouseCursor;
			bCapturedCursorState = false;
		}
	}
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
		PreviewMesh->SetRenderCustomDepth(false);
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

	if (bPlacementValid || !Definition->InvalidPreviewMaterial)
	{
		if (bUsingInvalidPreviewMaterial)
		{
			RestorePieceMaterials(Definition);
			InvalidPreviewMaterialInstances.Reset();
			bUsingInvalidPreviewMaterial = false;
		}
		return;
	}

	const int32 MaterialCount = FMath::Max(1, PreviewMesh->GetNumMaterials());
	if (!bUsingInvalidPreviewMaterial
		|| InvalidPreviewMaterialInstances.Num() != MaterialCount)
	{
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
	APlayerController* PlayerController = GetLocalPlayerController();
	UBuildStructureComponent* Structure = FindBuildStructure(PlayerController);
	CurrentStructure = Structure;
	if (!Structure)
	{
		DestroyPreviewMesh();
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_BadDefinition;
		return;
	}

	const UBuildPieceCatalog* Catalog = Structure->GetCatalog();
	const UBuildPieceDefinition* Definition = Catalog
		? Catalog->GetByIndex(SelectedPieceIndex)
		: nullptr;
	if (!Definition || !Definition->Mesh
		|| !ProjectCursorToStructure(PlayerController, Structure, CurrentCursorWorld))
	{
		DestroyPreviewMesh();
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_BadDefinition;
		return;
	}

	ConfigurePreviewMesh(Definition);
	IBuildStructureHost* Host = Structure->GetHost();
	USceneComponent* AttachRoot = Host ? Host->GetStructureAttachRoot() : nullptr;
	if (!PreviewMesh || !AttachRoot)
	{
		return;
	}

	if (PreviewMesh->GetAttachParent() != AttachRoot)
	{
		PreviewMesh->AttachToComponent(
			AttachRoot,
			FAttachmentTransformRules::KeepRelativeTransform);
	}

	CurrentKey = Structure->WorldToSlot(CurrentCursorWorld, Definition->SlotType);
	bCurrentPlacementValid = Structure->CanPlacePiece(
		CurrentKey,
		Definition,
		CurrentFailReason);
	if (bCurrentPlacementValid && !IsLocallyWithinRange(Structure, CurrentKey))
	{
		bCurrentPlacementValid = false;
		CurrentFailReason = BuildGameplayTags::Fail_TooFar;
	}

	FTransform PreviewTransform;
	if (bCurrentPlacementValid)
	{
		PreviewTransform = Structure->GetPieceRelativeTransform(CurrentKey, Definition, 0);
		ApplyPreviewMaterial(Definition, true, 0.0f);
		PreviewMesh->SetRenderCustomDepth(false);
		PreviewMesh->SetCustomDepthStencilValue(0);
	}
	else
	{
		const FTransform StructureSpace = Host->GetStructureSpace();
		FVector UnsnappedLocation = StructureSpace.InverseTransformPosition(CurrentCursorWorld);
		UnsnappedLocation.Z = CurrentKey.Coord.Level * Structure->GetGridSettings().LevelHeight;
		UnsnappedLocation += Definition->MeshOffset;

		PreviewTransform = FTransform(
			FRotator::ZeroRotator,
			UnsnappedLocation,
			Definition->MeshScale);

		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		const float ShakeAlpha = FailureShakeDuration > 0.0f
			? FMath::Clamp(
				static_cast<float>((FailureShakeEndSeconds - Now) / FailureShakeDuration),
				0.0f,
				1.0f)
			: 0.0f;
		const float ShakeAmplitude = FailureShakeAmplitude * ShakeAlpha;
		ApplyPreviewMaterial(Definition, false, ShakeAmplitude);
		PreviewMesh->SetRenderCustomDepth(false);
		PreviewMesh->SetCustomDepthStencilValue(0);
	}

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

void UBuildPreviewComponent::TriggerFailureFeedback(FGameplayTag FailReason)
{
	if (const UWorld* World = GetWorld())
	{
		FailureShakeEndSeconds = World->GetTimeSeconds() + FailureShakeDuration;
	}
	CurrentFailReason = FailReason;
}
