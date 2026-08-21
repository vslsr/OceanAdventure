// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureBuildInputWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureBuildInputWidget)

TOptional<FUIInputConfig> UOceanAdventureBuildInputWidget::GetDesiredInputConfig() const
{
	// All == game and menu input both run; NoCapture keeps the cursor free for placement.
	FUIInputConfig Config(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
	Config.bIgnoreLookInput = true;
	return Config;
}
