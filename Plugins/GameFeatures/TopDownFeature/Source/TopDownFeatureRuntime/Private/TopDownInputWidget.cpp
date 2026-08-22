// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

TOptional<FUIInputConfig> UTopDownInputWidget::GetDesiredInputConfig() const
{
	// CommonUI owns cursor visibility and restores the previous policy when this widget pops.
	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
}
