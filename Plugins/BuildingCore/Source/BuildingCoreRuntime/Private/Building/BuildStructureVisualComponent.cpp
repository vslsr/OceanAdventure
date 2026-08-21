// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildStructureVisualComponent.h"

#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureHost.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildStructureVisualComponent)

UBuildStructureVisualComponent::UBuildStructureVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UBuildStructureVisualComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		if (UBuildStructureComponent* Structure =
			OwnerActor->FindComponentByClass<UBuildStructureComponent>())
		{
			BuildComponent = Structure;
			Structure->OnStructureChanged.AddUObject(this, &ThisClass::RebuildInstances);
			RebuildInstances();
		}
	}
}

void UBuildStructureVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBuildStructureComponent* Structure = BuildComponent.Get())
	{
		Structure->OnStructureChanged.RemoveAll(this);
	}

	for (TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : MeshToISM)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	MeshToISM.Reset();

	Super::EndPlay(EndPlayReason);
}

UInstancedStaticMeshComponent* UBuildStructureVisualComponent::FindOrCreateISM(
	UStaticMesh* Mesh,
	const UBuildPieceDefinition* Definition)
{
	if (!Mesh)
	{
		return nullptr;
	}

	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISM.Find(Mesh))
	{
		return Found->Get();
	}

	AActor* OwnerActor = GetOwner();
	IBuildStructureHost* Host = OwnerActor ? Cast<IBuildStructureHost>(OwnerActor) : nullptr;
	USceneComponent* AttachRoot = Host ? Host->GetStructureAttachRoot() : nullptr;
	if (!OwnerActor || !AttachRoot)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* ISM =
		NewObject<UInstancedStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	ISM->SetStaticMesh(Mesh);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetGenerateOverlapEvents(false);
	ISM->SetCanEverAffectNavigation(false);
	ISM->SetupAttachment(AttachRoot);

	const UBuildPieceFragment_Collision* CollisionRules = Definition
		? Definition->FindFragment<UBuildPieceFragment_Collision>()
		: nullptr;
	if (!CollisionRules || CollisionRules->bBlocking)
	{
		ISM->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		ISM->CanCharacterStepUpOn = (!CollisionRules || CollisionRules->bCanCharacterStepUpOn)
			? ECB_Yes
			: ECB_No;
	}
	else
	{
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (Definition)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < Definition->OverrideMaterials.Num(); ++MaterialIndex)
		{
			if (UMaterialInterface* Material = Definition->OverrideMaterials[MaterialIndex])
			{
				ISM->SetMaterial(MaterialIndex, Material);
			}
		}
	}

	OwnerActor->AddInstanceComponent(ISM);
	ISM->RegisterComponent();
	ISM->SetRelativeTransform(FTransform::Identity);
	MeshToISM.Add(Mesh, ISM);
	return ISM;
}

void UBuildStructureVisualComponent::RebuildInstances()
{
	UBuildStructureComponent* Structure = BuildComponent.Get();
	if (!Structure)
	{
		return;
	}

	for (TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : MeshToISM)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}

	const UBuildPieceCatalog* Catalog = Structure->GetCatalog();
	if (!Catalog)
	{
		return;
	}

	for (const FBuildPieceEntry& Entry : Structure->GetEntries())
	{
		const UBuildPieceDefinition* Definition = Catalog->GetByIndex(Entry.PieceIndex);
		if (!Definition || !Definition->Mesh)
		{
			continue;
		}

		if (UInstancedStaticMeshComponent* ISM = FindOrCreateISM(Definition->Mesh, Definition))
		{
			ISM->AddInstance(
				Structure->GetPieceRelativeTransform(Entry.Key, Definition, Entry.Rotation),
				false);
		}
	}
}
