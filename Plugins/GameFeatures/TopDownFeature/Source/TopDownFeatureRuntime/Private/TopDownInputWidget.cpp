// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

TOptional<FUIInputConfig> UTopDownInputWidget::GetDesiredInputConfig() const
{
	// CommonUI owns cursor visibility and restores the previous policy when this widget pops.
	//
	// All + NoCapture is deliberate and verified: the naval fire logs show a single left
	// click reaching the ability system intact, so mouse buttons are not being held back
	// here. Do not switch this to Game or to a capturing mode to chase an input bug --
	// capturing the viewport is what stops the right-drag camera rotation from working.
	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
}
