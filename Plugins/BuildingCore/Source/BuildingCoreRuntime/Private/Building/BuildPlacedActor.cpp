// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildPlacedActor.h"

#include "Building/BuildPieceDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildPlacedActor)

ABuildPlacedActor::ABuildPlacedActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	MeshComponent->SetCanEverAffectNavigation(false);

	bReplicates = true;
	// The Actor is attached to the host's root, so its transform follows the host without any
	// movement replication of its own; replicating movement as well would fight the attachment.
	SetReplicateMovement(false);
	NetDormancy = DORM_Initial;
}

void ABuildPlacedActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABuildPlacedActor, SourceDefinition);
}

void ABuildPlacedActor::SetSourceDefinition(const UBuildPieceDefinition* Definition)
{
	if (!HasAuthority())
	{
		return;
	}

	SourceDefinition = Definition;
	ApplySourceDefinition();
}

void ABuildPlacedActor::OnRep_SourceDefinition()
{
	ApplySourceDefinition();
}

void ABuildPlacedActor::ApplySourceDefinition()
{
	if (!SourceDefinition || !MeshComponent)
	{
		return;
	}

	MeshComponent->SetStaticMesh(SourceDefinition->Mesh);
	MeshComponent->SetRelativeScale3D(SourceDefinition->MeshScale);

	for (int32 MaterialIndex = 0; MaterialIndex < SourceDefinition->OverrideMaterials.Num(); ++MaterialIndex)
	{
		if (UMaterialInterface* Material = SourceDefinition->OverrideMaterials[MaterialIndex])
		{
			MeshComponent->SetMaterial(MaterialIndex, Material);
		}
	}
}
