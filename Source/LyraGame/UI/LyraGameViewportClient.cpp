// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraGameViewportClient.h"

#include "CommonUISettings.h"
#include "ICommonUIModule.h"
#include "LyraLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraGameViewportClient)

class UGameInstance;

namespace GameViewportTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Platform_Trait_Input_HardwareCursor, "Platform.Trait.Input.HardwareCursor");
}

ULyraGameViewportClient::ULyraGameViewportClient()
	: Super(FObjectInitializer::Get())
{
}

void ULyraGameViewportClient::Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	
	// We have software cursors set up in our project settings for console/mobile use, but on desktop we're fine with
	// the standard hardware cursors
	const bool UseHardwareCursor = ICommonUIModule::GetSettings().GetPlatformTraits().HasTag(GameViewportTags::TAG_Platform_Trait_Input_HardwareCursor);
	SetUseSoftwareCursorWidgets(!UseHardwareCursor);
}

bool ULyraGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// TEMPORARY DIAGNOSTIC -- remove once the mouse-button investigation is closed.
	const bool bIsWatchedMouseButton =
		EventArgs.Key == EKeys::LeftMouseButton || EventArgs.Key == EKeys::RightMouseButton;

	const bool bHandled = Super::InputKey(EventArgs);

	if (bIsWatchedMouseButton)
	{
		// handled=1 means something above the player controller claimed this press. That is
		// the difference between a click that is on its way to gameplay and one that is not.
		UE_LOG(
			LogLyra,
			Display,
			TEXT("[MouseProbe/Viewport] key=%s event=%d handled=%d"),
			*EventArgs.Key.ToString(),
			static_cast<int32>(EventArgs.Event),
			bHandled);
	}

	return bHandled;
}
