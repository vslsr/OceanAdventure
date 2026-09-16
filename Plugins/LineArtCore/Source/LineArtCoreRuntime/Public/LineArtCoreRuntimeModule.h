// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

LINEARTCORERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogLineArtCore, Log, All);

/**
 * Maps this plugin's Shaders/ directory to the virtual path /Plugin/LineArtCore so the
 * line-art master materials can #include LineArtEnvironment.ush from a Custom node.
 *
 * The mapping has to exist before any shader compiles, which is why the module loads at
 * PostConfigInit: a material compiled earlier would fail to resolve the include and fall
 * back to the default (lit, wrong-coloured) output with only a log warning.
 */
class FLineArtCoreRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
