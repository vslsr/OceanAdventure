// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownFeatureGameplayTags.h"

namespace TopDownFeatureGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownClick, "InputTag.TopDownClick", "Sets a top-down movement target under the mouse cursor.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownMoveForward, "InputTag.TopDown.MoveForward", "Moves a top-down pawn forward relative to the camera.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownMoveBackward, "InputTag.TopDown.MoveBackward", "Moves a top-down pawn backward relative to the camera.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownMoveRight, "InputTag.TopDown.MoveRight", "Moves a top-down pawn right relative to the camera.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownMoveLeft, "InputTag.TopDown.MoveLeft", "Moves a top-down pawn left relative to the camera.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownCameraZoom, "InputTag.TopDown.Camera.Zoom", "Zooms the top-down camera with an axis input.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownCameraRotateHold, "InputTag.TopDown.Camera.RotateHold", "Enables top-down camera rotation while held.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_TopDownCameraRotate, "InputTag.TopDown.Camera.Rotate", "Rotates the top-down camera from pointer delta input.");
}
