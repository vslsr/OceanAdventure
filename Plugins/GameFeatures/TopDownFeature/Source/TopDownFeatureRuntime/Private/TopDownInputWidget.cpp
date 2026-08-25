// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

DEFINE_LOG_CATEGORY_STATIC(LogTopDownInputWidget, Log, All);

TOptional<FUIInputConfig> UTopDownInputWidget::GetDesiredInputConfig() const
{
	// CommonUI owns cursor visibility and restores the previous policy when this widget pops.
	//
	// All + CaptureDuringMouseDown is required for a game viewport with a visible cursor:
	// FSceneViewport only forwards an ordinary mouse-down to PlayerInput when it is already
	// captured or the capture mode requests a temporary capture. With NoCapture, the release
	// is forwarded but the press is dropped; that is exactly the single-click/double-click
	// symptom seen in the naval fire logs. Keep the cursor visible during the temporary capture
	// so this policy does not turn a top-down pointer into a hidden FPS cursor.
	//
	// Pushing this widget and CommonUI actually asking it for a config are two different
	// events, and only the second one changes how the pointer behaves. Logging the query
	// makes the difference readable instead of assumed.
	UE_LOG(LogTopDownInputWidget, Display,
		TEXT("[TopDownInput] Input config queried by CommonUI: mode=All capture=CaptureDuringMouseDown hide_cursor=0 activated=%d widget=%s"),
		IsActivated(), *GetName());

	return FUIInputConfig(
		ECommonInputMode::All,
		EMouseCaptureMode::CaptureDuringMouseDown,
		/*bHideCursorDuringViewportCapture=*/false);
}
