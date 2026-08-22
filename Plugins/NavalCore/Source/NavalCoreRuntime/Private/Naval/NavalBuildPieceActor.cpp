// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalBuildPieceActor.h"

#include "Building/BuildPieceDefinition.h"
#include "Naval/NavalFireWindowComponent.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalPieceFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalBuildPieceActor)

ANavalBuildPieceActor::ANavalBuildPieceActor()
{
	// Durability and construction state change while players are looking at them, so this
	// Actor cannot stay dormant the way a decorative placed prop can.
	NetDormancy = DORM_Never;

	PartComponent = CreateDefaultSubobject<UNavalPartComponent>(TEXT("PartComponent"));
}

void ANavalBuildPieceActor::ApplySourceDefinition()
{
	Super::ApplySourceDefinition();

	const UBuildPieceDefinition* Definition = GetSourceDefinition();
	if (!Definition || !PartComponent)
	{
		return;
	}

	if (const UBuildPieceFragment_NavalPart* PartFragment =
			Definition->FindFragment<UBuildPieceFragment_NavalPart>())
	{
		PartComponent->ConfigureFromFragment(
			PartFragment->PartType,
			PartFragment->MaxDurability,
			PartFragment->DeploySeconds,
			PartFragment->ConstructionSeconds);
	}

	if (const UBuildPieceFragment_NavalLoad* LoadFragment =
			Definition->FindFragment<UBuildPieceFragment_NavalLoad>())
	{
		PartComponent->SetLoadContribution(
			LoadFragment->Tonnage,
			LoadFragment->BuoyancyCapacity,
			LoadFragment->Thrust,
			LoadFragment->Steering);
	}

	if (const UBuildPieceFragment_NavalFireWindow* WindowFragment =
			Definition->FindFragment<UBuildPieceFragment_NavalFireWindow>())
	{
		// Created rather than always present: a wall that carried a dormant window component
		// would still answer window queries, and the rule has to be unambiguous.
		if (!FireWindowComponent)
		{
			FireWindowComponent = NewObject<UNavalFireWindowComponent>(this, TEXT("FireWindowComponent"));
			FireWindowComponent->RegisterComponent();
		}
		FireWindowComponent->Configure(
			WindowFragment->OutwardHalfAngleDegrees,
			WindowFragment->InnerAuthorizedDepth,
			WindowFragment->OpenWindupSeconds);
	}

	// 部署前摇 -> 实体施工 -> 完成启用, started once, on the server only.
	if (HasAuthority() && !bConstructionStarted)
	{
		bConstructionStarted = true;
		PartComponent->BeginConstruction();
	}
}
