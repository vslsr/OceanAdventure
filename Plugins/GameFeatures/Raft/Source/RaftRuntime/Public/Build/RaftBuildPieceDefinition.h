// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildPieceDefinition.h"

#include "RaftBuildPieceDefinition.generated.h"

/** Raft-owned whole-module definition; mesh and alignment remain data-asset configured. */
UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftBuildPieceDefinition final : public UBuildPieceDefinition
{
	GENERATED_BODY()

public:
	URaftBuildPieceDefinition();

	virtual void PostLoad() override;
};
