// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"

#include "TopDownCameraDragInputWidget.generated.h"

struct FUIInputConfig;

/** CommonUI input policy active only while the player holds the top-down rotate action. */
UCLASS()
class TOPDOWNFEATURERUNTIME_API UTopDownCameraDragInputWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
