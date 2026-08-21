// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"

#include "OceanAdventureBuildInputWidget.generated.h"

/**
 * Input policy for build mode, expressed the CommonUI way.
 *
 * While this widget is on the UI stack the cursor is visible and uncaptured, and both game and
 * menu input are routed. Popping it restores whatever the layer below asked for, which is why
 * nothing here touches APlayerController::bShowMouseCursor.
 */
UCLASS(Blueprintable)
class OCEANADVENTURERUNTIME_API UOceanAdventureBuildInputWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
