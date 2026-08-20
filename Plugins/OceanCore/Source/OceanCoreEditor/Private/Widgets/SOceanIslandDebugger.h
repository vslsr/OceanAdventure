// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OceanGenerationSettings.h"

class IDetailsView;
class SImage;
class STextBlock;
class UTexture2D;
struct FPropertyChangedEvent;

/** Editor-only parameter surface and deterministic noise-map preview. */
class SOceanIslandDebugger : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOceanIslandDebugger) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

#if WITH_DEV_AUTOMATION_TESTS
	bool HasValidPreviewForTesting() const;
#endif

private:
	FString GetSettingsObjectPath() const;
	void HandleSettingsAssetChanged(const FAssetData& AssetData);
	void HandleSettingsPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);
	FReply HandleCreateSettings();
	FReply HandleUseSelectedSettings();
	void HandleCenterXCommitted(int32 NewValue, ETextCommit::Type CommitType);
	void HandleCenterYCommitted(int32 NewValue, ETextCommit::Type CommitType);
	void HandlePreviewRadiusCommitted(int32 NewValue, ETextCommit::Type CommitType);
	void SetSettings(UOceanGenerationSettings* NewSettings);
	void RefreshPreview();
	void RebuildNoiseTexture();
	FText BuildValidationText() const;
	FSlateColor GetValidationColor() const;

	TStrongObjectPtr<UOceanGenerationSettings> Settings;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SImage> PreviewImage;
	TSharedPtr<STextBlock> SummaryText;
	TSharedPtr<STextBlock> ValidationText;
	TStrongObjectPtr<UTexture2D> PreviewTexture;
	FSlateBrush PreviewBrush;
	FIntPoint PreviewCenter = FIntPoint::ZeroValue;
	int32 PreviewRadius = 3;
	int32 PreviewTextureSize = 0;
};
