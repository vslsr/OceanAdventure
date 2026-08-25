// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownInputWidget.h"

#include "CommonInputModeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownInputWidget)

TOptional<FUIInputConfig> UTopDownInputWidget::GetDesiredInputConfig() const
{
	// CommonUI owns cursor visibility and restores the previous policy when this widget pops.
	//
	// Game rather than All, and a capture mode that spells out IncludingInitialMouseDown:
	// under All + NoCapture the player controller runs in Game-and-UI with an uncaptured
	// viewport, so mouse buttons are hit-tested against Slate before the game ever sees them
	// while keyboard keys still reach it. That asymmetry shows up as gameplay that answers
	// the keyboard immediately but needs a second click before a mouse button registers --
	// the first click only goes to restoring viewport focus.
	//
	// The cursor stays visible because top-down aiming reads it; only the routing changes.
	return FUIInputConfig(
		ECommonInputMode::Game,
		EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown,
		/*bHideCursorDuringViewportCapture=*/false);
}
