// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SOceanIslandDebugger.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Factories/DataAssetFactory.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/OceanGenerationSettings.h"

#define LOCTEXT_NAMESPACE "SOceanIslandDebugger"

namespace
{
	constexpr int32 MaxPreviewTextureSize = 768;
	const FLinearColor WaterLegendColor(0.03f, 0.38f, 0.67f);
	const FLinearColor EdgeLegendColor(1.0f, 0.86f, 0.22f);
	const FLinearColor LandLegendColor(0.22f, 0.68f, 0.20f);

	enum class ESettingsValidationIssue : uint8
	{
		None,
		IslandBelowWater,
		OceanFloorAboveWater,
		IslandTooLarge,
		NegativeBeachBand
	};

	TSharedRef<SWidget> MakeLegendItem(FLinearColor Color, const FText& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Color)
				.Padding(FMargin(7.0f, 5.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.TextStyle(FAppStyle::Get(), "SmallText")
			];
	}

	FColor GetNoiseMapColor(float Height, float WaterHeight, float BeachBandHeight)
	{
		if (Height <= WaterHeight)
		{
			return WaterLegendColor.ToFColorSRGB();
		}

		if (Height <= WaterHeight + BeachBandHeight)
		{
			return EdgeLegendColor.ToFColorSRGB();
		}

		return LandLegendColor.ToFColorSRGB();
	}

	ESettingsValidationIssue GetValidationIssue(const UOceanGenerationSettings& Settings)
	{
		if (Settings.GetIslandPeakHeight() <= Settings.GetWaterHeight())
		{
			return ESettingsValidationIssue::IslandBelowWater;
		}

		if (Settings.GetOceanFloorHeight() >= Settings.GetWaterHeight())
		{
			return ESettingsValidationIssue::OceanFloorAboveWater;
		}

		const float MaximumIslandReach = Settings.GetIslandRadius() * 1.4f
			* (1.0f + Settings.GetIslandEdgeDistortion()) + Settings.GetUnderwaterShelfWidth();
		if (MaximumIslandReach > Settings.GetIslandCellSize())
		{
			return ESettingsValidationIssue::IslandTooLarge;
		}

		if (Settings.GetBeachBandHeight() < 0.0f)
		{
			return ESettingsValidationIssue::NegativeBeachBand;
		}

		return ESettingsValidationIssue::None;
	}
}

void SOceanIslandDebugger::Construct(const FArguments& InArgs)
{
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	DetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	DetailsView->OnFinishedChangingProperties().AddSP(
		this, &SOceanIslandDebugger::HandleSettingsPropertyChanged);

	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageType = ESlateBrushImageType::FullColor;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UOceanGenerationSettings::StaticClass())
				.ObjectPath(this, &SOceanIslandDebugger::GetSettingsObjectPath)
				.OnObjectChanged(this, &SOceanIslandDebugger::HandleSettingsAssetChanged)
				.DisplayBrowse(true)
				.DisplayThumbnail(true)
				.DisplayUseSelected(false)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("UseSelectedTooltip", "Use the selected Ocean Generation Settings asset."))
				.OnClicked(this, &SOceanIslandDebugger::HandleUseSelectedSettings)
				.ContentPadding(FMargin(6.0f, 3.0f))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.ArrowLeft"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("CreateSettingsTooltip", "Create an Ocean Generation Settings asset."))
				.OnClicked(this, &SOceanIslandDebugger::HandleCreateSettings)
				.ContentPadding(FMargin(6.0f, 3.0f))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Plus"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f, 6.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(SummaryText, STextBlock)
				.TextStyle(FAppStyle::Get(), "SmallText")
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(ValidationText, STextBlock)
				.TextStyle(FAppStyle::Get(), "SmallText")
				.ColorAndOpacity(this, &SOceanIslandDebugger::GetValidationColor)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.34f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
				.Padding(4.0f)
				[
					DetailsView.ToSharedRef()
				]
			]
			+ SSplitter::Slot()
			.Value(0.66f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(6.0f, 2.0f, 6.0f, 6.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(3.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SNumericEntryBox<int32>)
						.LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("CenterX", "Chunk X"))]
						.Value_Lambda([this] { return PreviewCenter.X; })
						.OnValueCommitted(this, &SOceanIslandDebugger::HandleCenterXCommitted)
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SNumericEntryBox<int32>)
						.LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("CenterY", "Chunk Y"))]
						.Value_Lambda([this] { return PreviewCenter.Y; })
						.OnValueCommitted(this, &SOceanIslandDebugger::HandleCenterYCommitted)
					]
					+ SUniformGridPanel::Slot(2, 0)
					[
						SNew(SNumericEntryBox<int32>)
						.LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("PreviewRadius", "Radius"))]
						.MinValue(0)
						.MaxValue(8)
						.AllowSpin(true)
						.Value_Lambda([this] { return PreviewRadius; })
						.OnValueCommitted(this, &SOceanIslandDebugger::HandlePreviewRadiusCommitted)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(9.0f, 0.0f, 6.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						MakeLegendItem(WaterLegendColor, LOCTEXT("WaterLegend", "Water"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						MakeLegendItem(EdgeLegendColor, LOCTEXT("EdgeLegend", "Island Edge"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						MakeLegendItem(LandLegendColor, LOCTEXT("LandLegend", "Island"))
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(6.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(256.0f)
					.MinDesiredHeight(256.0f)
					.MaxDesiredWidth(768.0f)
					.MaxDesiredHeight(768.0f)
					.WidthOverride_Lambda([this] { return static_cast<float>(FMath::Max(256, PreviewTextureSize)); })
					.HeightOverride_Lambda([this] { return static_cast<float>(FMath::Max(256, PreviewTextureSize)); })
					[
						SAssignNew(PreviewImage, SImage)
						.Image(&PreviewBrush)
					]
				]
			]
		]
	];

	SetSettings(nullptr);
}

FString SOceanIslandDebugger::GetSettingsObjectPath() const
{
	return Settings.IsValid() && !Settings->HasAnyFlags(RF_Transient)
		? Settings->GetPathName()
		: FString();
}

void SOceanIslandDebugger::HandleSettingsAssetChanged(const FAssetData& AssetData)
{
	SetSettings(Cast<UOceanGenerationSettings>(AssetData.GetAsset()));
}

void SOceanIslandDebugger::HandleSettingsPropertyChanged(
	const FPropertyChangedEvent& PropertyChangedEvent)
{
	RefreshPreview();
}

FReply SOceanIslandDebugger::HandleCreateSettings()
{
	FSaveAssetDialogConfig SaveConfig;
	SaveConfig.DialogTitleOverride = LOCTEXT("CreateSettingsDialogTitle", "Create Ocean Generation Settings");
	SaveConfig.DefaultPath = TEXT("/Game/Ocean");
	SaveConfig.DefaultAssetName = TEXT("DA_OceanGenerationSettings");
	SaveConfig.AssetClassNames.Add(UOceanGenerationSettings::StaticClass()->GetClassPathName());
	SaveConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;

	FContentBrowserModule& ContentBrowser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	const FString ObjectPath = ContentBrowser.Get().CreateModalSaveAssetDialog(SaveConfig);
	if (ObjectPath.IsEmpty())
	{
		return FReply::Handled();
	}

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UOceanGenerationSettings::StaticClass();
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	UObject* NewAsset = AssetTools.CreateAsset(
		FPackageName::GetLongPackageAssetName(ObjectPath),
		FPackageName::GetLongPackagePath(ObjectPath),
		UOceanGenerationSettings::StaticClass(),
		Factory);
	SetSettings(Cast<UOceanGenerationSettings>(NewAsset));
	return FReply::Handled();
}

FReply SOceanIslandDebugger::HandleUseSelectedSettings()
{
	TArray<FAssetData> SelectedAssets;
	FContentBrowserModule& ContentBrowser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowser.Get().GetSelectedAssets(SelectedAssets);
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (UOceanGenerationSettings* SelectedSettings =
			Cast<UOceanGenerationSettings>(AssetData.GetAsset()))
		{
			SetSettings(SelectedSettings);
			break;
		}
	}
	return FReply::Handled();
}

void SOceanIslandDebugger::HandleCenterXCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	PreviewCenter.X = NewValue;
	RefreshPreview();
}

void SOceanIslandDebugger::HandleCenterYCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	PreviewCenter.Y = NewValue;
	RefreshPreview();
}

void SOceanIslandDebugger::HandlePreviewRadiusCommitted(
	int32 NewValue, ETextCommit::Type CommitType)
{
	PreviewRadius = FMath::Clamp(NewValue, 0, 8);
	RefreshPreview();
}

void SOceanIslandDebugger::SetSettings(UOceanGenerationSettings* NewSettings)
{
	if (!NewSettings)
	{
		NewSettings = NewObject<UOceanGenerationSettings>(
			GetTransientPackage(), NAME_None, RF_Transient);
	}

	Settings.Reset(NewSettings);
	DetailsView->SetObject(NewSettings);
	RefreshPreview();
}

#if WITH_DEV_AUTOMATION_TESTS
bool SOceanIslandDebugger::HasValidPreviewForTesting() const
{
	return Settings.IsValid()
		&& PreviewTexture.IsValid()
		&& PreviewTextureSize > 0
		&& PreviewBrush.GetResourceObject() == PreviewTexture.Get();
}
#endif

void SOceanIslandDebugger::RefreshPreview()
{
	RebuildNoiseTexture();

	if (SummaryText)
	{
		const int32 ChunkCount = PreviewRadius * 2 + 1;
		SummaryText->SetText(Settings.IsValid()
			? FText::Format(
				LOCTEXT("PreviewSummary", "{0} x {0} chunks  |  {1} px  |  Seed {2}"),
				FText::AsNumber(ChunkCount),
				FText::AsNumber(PreviewTextureSize),
				FText::AsNumber(Settings->GetWorldSeed()))
			: LOCTEXT("NoSettingsSummary", "No generation settings selected"));
	}

	if (ValidationText)
	{
		ValidationText->SetText(BuildValidationText());
	}

	if (PreviewImage)
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SOceanIslandDebugger::RebuildNoiseTexture()
{
	PreviewTextureSize = 0;
	PreviewBrush.SetResourceObject(nullptr);
	PreviewTexture.Reset();

	UOceanGenerationSettings* CurrentSettings = Settings.Get();
	if (!CurrentSettings)
	{
		return;
	}

	const int32 ChunkCount = PreviewRadius * 2 + 1;
	const int32 DesiredPixelsPerChunk = FMath::Clamp(CurrentSettings->GetTerrainResolution(), 16, 96);
	const int32 PixelsPerChunk = FMath::Max(
		8, FMath::Min(DesiredPixelsPerChunk, MaxPreviewTextureSize / ChunkCount));
	PreviewTextureSize = ChunkCount * PixelsPerChunk;

	const float ChunkSize = CurrentSettings->GetChunkSize();
	const FIntPoint MinChunk = PreviewCenter - FIntPoint(PreviewRadius, PreviewRadius);
	const float WaterHeight = CurrentSettings->GetWaterHeight();
	const float BeachBandHeight = CurrentSettings->GetBeachBandHeight();
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(PreviewTextureSize * PreviewTextureSize);

	for (int32 PixelY = 0; PixelY < PreviewTextureSize; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < PreviewTextureSize; ++PixelX)
		{
			const FVector2D WorldPosition(
				(static_cast<double>(MinChunk.X)
					+ (static_cast<double>(PixelX) + 0.5) / PixelsPerChunk) * ChunkSize,
				(static_cast<double>(MinChunk.Y + ChunkCount)
					- (static_cast<double>(PixelY) + 0.5) / PixelsPerChunk) * ChunkSize);

			const float Height = CurrentSettings->SampleTerrainHeight(WorldPosition);
			FColor Color = GetNoiseMapColor(Height, WaterHeight, BeachBandHeight);
			const bool bChunkGrid = PixelX % PixelsPerChunk == 0 || PixelY % PixelsPerChunk == 0;
			if (bChunkGrid)
			{
				Color = FLinearColor::LerpUsingHSV(
					FLinearColor(Color), FLinearColor(FColor(12, 18, 24)), 0.58f).ToFColorSRGB();
			}
			Pixels[PixelY * PreviewTextureSize + PixelX] = Color;
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		PreviewTextureSize, PreviewTextureSize, PF_B8G8R8A8, NAME_None);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		PreviewTextureSize = 0;
		return;
	}

	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->SRGB = true;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	PreviewTexture.Reset(Texture);
	PreviewBrush.SetResourceObject(Texture);
	PreviewBrush.ImageSize = FVector2D(PreviewTextureSize, PreviewTextureSize);
}

FText SOceanIslandDebugger::BuildValidationText() const
{
	const UOceanGenerationSettings* CurrentSettings = Settings.Get();
	if (!CurrentSettings)
	{
		return FText::GetEmpty();
	}

	switch (GetValidationIssue(*CurrentSettings))
	{
	case ESettingsValidationIssue::IslandBelowWater:
		return LOCTEXT("IslandBelowWater", "Island peak must be above the waterline");
	case ESettingsValidationIssue::OceanFloorAboveWater:
		return LOCTEXT("OceanFloorAboveWater", "Ocean floor must be below the waterline");
	case ESettingsValidationIssue::IslandTooLarge:
		return LOCTEXT("IslandTooLarge", "Island and underwater shelf exceed the sampled cell range");
	case ESettingsValidationIssue::NegativeBeachBand:
		return LOCTEXT("NegativeBeachBand", "Beach band height cannot be negative");
	default:
		return LOCTEXT("ParametersValid", "Parameters valid");
	}
}

FSlateColor SOceanIslandDebugger::GetValidationColor() const
{
	const UOceanGenerationSettings* CurrentSettings = Settings.Get();
	if (!CurrentSettings)
	{
		return FSlateColor::UseForeground();
	}

	const bool bValid = GetValidationIssue(*CurrentSettings) == ESettingsValidationIssue::None;
	return bValid
		? FSlateColor(FLinearColor(0.18f, 0.72f, 0.30f))
		: FSlateColor(FLinearColor(1.0f, 0.62f, 0.12f));
}

#undef LOCTEXT_NAMESPACE
