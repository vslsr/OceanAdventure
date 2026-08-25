// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

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
	return FUIInputConfig(
		ECommonInputMode::All,
		EMouseCaptureMode::CaptureDuringMouseDown,
		/*bHideCursorDuringViewportCapture=*/false);
}
