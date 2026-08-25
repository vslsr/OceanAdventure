// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

DEFINE_LOG_CATEGORY_STATIC(LogTopDownInputWidget, Log, All);

TOptional<FUIInputConfig> UTopDownInputWidget::GetDesiredInputConfig() const
{
	// CommonUI owns cursor visibility and restores the previous policy when this widget pops.
	//
	// All + NoCapture is deliberate and verified: the naval fire logs show a single left
	// click reaching the ability system intact, so mouse buttons are not being held back
	// here. Do not switch this to Game or to a capturing mode to chase an input bug --
	// capturing the viewport is what stops the right-drag camera rotation from working.
	//
	// Pushing this widget and CommonUI actually asking it for a config are two different
	// events, and only the second one changes how the pointer behaves. Logging the query
	// makes the difference readable instead of assumed.
	UE_LOG(LogTopDownInputWidget, Display,
		TEXT("[TopDownInput] Input config queried by CommonUI: mode=All capture=NoCapture activated=%d widget=%s"),
		IsActivated(), *GetName());

	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
}
