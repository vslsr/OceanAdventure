// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildStructureHost.h"

#include "Components/InstancedStaticMeshComponent.h"

TSubclassOf<UInstancedStaticMeshComponent> IBuildStructureHost::GetInstancedMeshComponentClass() const
{
	return UInstancedStaticMeshComponent::StaticClass();
}
