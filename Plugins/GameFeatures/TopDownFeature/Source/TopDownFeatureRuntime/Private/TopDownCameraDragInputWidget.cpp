// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownCameraDragInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownCameraDragInputWidget)

TOptional<FUIInputConfig> UTopDownCameraDragInputWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
}
