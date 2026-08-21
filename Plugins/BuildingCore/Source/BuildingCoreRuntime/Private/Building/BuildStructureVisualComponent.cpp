// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildStructureVisualComponent.h"

#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPlacedActor.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureHost.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
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
	DestroySpawnedActors();

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

	// The host picks the component class: ISM for something that moves, HISM for a big static
	// footprint that wants per-instance culling.
	const TSubclassOf<UInstancedStaticMeshComponent> ComponentClass = Host->GetInstancedMeshComponentClass();
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(
		OwnerActor,
		ComponentClass ? ComponentClass.Get() : UInstancedStaticMeshComponent::StaticClass(),
		NAME_None,
		RF_Transient);
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

	RefreshSpawnedActors();

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

		// A piece that spawns an Actor is drawn by that Actor, not as an instance.
		if (Definition->FindFragment<UBuildPieceFragment_SpawnActor>())
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

void UBuildStructureVisualComponent::RefreshSpawnedActors()
{
	AActor* OwnerActor = GetOwner();
	UBuildStructureComponent* Structure = BuildComponent.Get();
	UWorld* World = GetWorld();
	if (!OwnerActor || !Structure || !World)
	{
		return;
	}

	// The Actors replicate on their own; spawning them per peer would duplicate them.
	if (!OwnerActor->HasAuthority())
	{
		return;
	}

	IBuildStructureHost* Host = Cast<IBuildStructureHost>(OwnerActor);
	USceneComponent* AttachRoot = Host ? Host->GetStructureAttachRoot() : nullptr;
	const UBuildPieceCatalog* Catalog = Structure->GetCatalog();
	if (!AttachRoot || !Catalog)
	{
		return;
	}

	TSet<FBuildSlotKey> DesiredKeys;
	for (const FBuildPieceEntry& Entry : Structure->GetEntries())
	{
		const UBuildPieceDefinition* Definition = Catalog->GetByIndex(Entry.PieceIndex);
		const UBuildPieceFragment_SpawnActor* SpawnFragment = Definition
			? Definition->FindFragment<UBuildPieceFragment_SpawnActor>()
			: nullptr;
		if (!SpawnFragment || !SpawnFragment->ActorClass)
		{
			continue;
		}

		DesiredKeys.Add(Entry.Key);
		if (const TWeakObjectPtr<AActor>* Existing = SpawnedActors.Find(Entry.Key);
			Existing && Existing->IsValid())
		{
			// Already standing. Leaving it alone is the whole point of diffing: respawning
			// would reset whatever state the Actor is holding.
			continue;
		}

		FTransform SpawnTransform = Structure->GetPieceRelativeTransform(
			Entry.Key,
			Definition,
			Entry.Rotation);
		SpawnTransform.AddToTranslation(SpawnFragment->SpawnOffset);
		SpawnTransform = SpawnTransform * AttachRoot->GetComponentTransform();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = OwnerActor;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedActor = World->SpawnActor<AActor>(
			SpawnFragment->ActorClass,
			SpawnTransform,
			SpawnParameters);
		if (!SpawnedActor)
		{
			continue;
		}

		// Attaching to the host's root is what makes props ride a moving raft for free.
		SpawnedActor->AttachToComponent(
			AttachRoot,
			FAttachmentTransformRules::KeepWorldTransform);

		// Framework-provided Actors take their visuals from the same definition the ghost used,
		// so the preview and the placed thing cannot drift apart.
		if (ABuildPlacedActor* PlacedActor = Cast<ABuildPlacedActor>(SpawnedActor))
		{
			PlacedActor->SetSourceDefinition(Definition);
		}

		SpawnedActors.Add(Entry.Key, SpawnedActor);
	}

	for (auto It = SpawnedActors.CreateIterator(); It; ++It)
	{
		if (DesiredKeys.Contains(It.Key()))
		{
			continue;
		}
		if (AActor* SpawnedActor = It.Value().Get())
		{
			SpawnedActor->Destroy();
		}
		It.RemoveCurrent();
	}
}

void UBuildStructureVisualComponent::DestroySpawnedActors()
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		for (TPair<FBuildSlotKey, TWeakObjectPtr<AActor>>& Pair : SpawnedActors)
		{
			if (AActor* SpawnedActor = Pair.Value.Get())
			{
				SpawnedActor->Destroy();
			}
		}
	}
	SpawnedActors.Reset();
}
