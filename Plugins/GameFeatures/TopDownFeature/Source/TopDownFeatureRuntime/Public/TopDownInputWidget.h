// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"

#include "TopDownInputWidget.generated.h"

/** CommonUI input policy used while a top-down pawn is locally controlled. */
UCLASS()
class TOPDOWNFEATURERUNTIME_API UTopDownInputWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
