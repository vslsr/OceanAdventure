// Copyright 2026 Nwiro. All Rights Reserved.
#include "NwiroIKStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"

TSharedPtr<FSlateStyleSet> FNwiroIKStyle::StyleInstance = nullptr;

void FNwiroIKStyle::Initialize()
{
    if (!StyleInstance.IsValid())
    {
        StyleInstance = MakeShareable(new FSlateStyleSet("NwiroIKStyle"));

        FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("NwiroIntegrationKit"))->GetBaseDir() / TEXT("Resources");
        StyleInstance->SetContentRoot(ContentDir);

        FString IconPath = StyleInstance->RootToContentDir(TEXT("Icon128"), TEXT(".png"));
        StyleInstance->Set("NwiroIK.Logo", new FSlateImageBrush(IconPath, FVector2D(16, 16)));

        FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
    }
}

void FNwiroIKStyle::Shutdown()
{
    if (StyleInstance.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
        StyleInstance.Reset();
    }
}
