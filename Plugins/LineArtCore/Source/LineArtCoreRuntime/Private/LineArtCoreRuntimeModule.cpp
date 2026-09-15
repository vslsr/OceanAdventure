// Copyright Epic Games, Inc. All Rights Reserved.

#include "LineArtCoreRuntimeModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogLineArtCore);

#define LOCTEXT_NAMESPACE "FLineArtCoreRuntimeModule"

void FLineArtCoreRuntimeModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LineArtCore"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogLineArtCore, Error, TEXT("LineArtCore plugin not found; the line-art shader include path is unavailable."));
		return;
	}

	const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/LineArtCore"), ShaderDirectory);
}

void FLineArtCoreRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLineArtCoreRuntimeModule, LineArtCoreRuntime)
