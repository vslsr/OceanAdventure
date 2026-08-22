// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureBuildInputWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureBuildInputWidget)

TOptional<FUIInputConfig> UOceanAdventureBuildInputWidget::GetDesiredInputConfig() const
{
	// CommonUI shows the mouse cursor for GameAndMenu input. Keeping game input enabled is
	// required so B can reach the active build ability again and cancel it.
	FUIInputConfig Config(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
	Config.bIgnoreLookInput = true;
	return Config;
}
