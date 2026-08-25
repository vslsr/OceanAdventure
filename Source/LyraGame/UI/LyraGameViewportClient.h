// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonGameViewportClient.h"

#include "LyraGameViewportClient.generated.h"

class UGameInstance;
class UObject;

UCLASS(BlueprintType)
class ULyraGameViewportClient : public UCommonGameViewportClient
{
	GENERATED_BODY()

public:
	ULyraGameViewportClient();

	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;

	// TEMPORARY DIAGNOSTIC -- remove once the mouse-button investigation is closed.
	// Slate hands viewport input to this function, and CommonUI's ECommonInputMode filtering
	// happens inside the Super call. Logging on the way in separates "a widget consumed the
	// click before the viewport ever saw it" from "the viewport saw it and CommonUI dropped
	// it", which nothing below this point can tell apart.
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
};
