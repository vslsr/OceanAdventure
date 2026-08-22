// Copyright Epic Games, Inc. All Rights Reserved.

#include "RaftRuntimeModule.h"

#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FRaftRuntimeModule"

DEFINE_LOG_CATEGORY(LogRaft);

void FRaftRuntimeModule::StartupModule()
{
	// Explicitly loaded GameFeature plugins do not inherit the project's tag search paths.
	// Register this feature's own config directory when its runtime module is loaded so editor
	// asset scripts and runtime code resolve the same Raft-owned tags.
	const TSharedPtr<IPlugin> RaftPlugin = IPluginManager::Get().FindPlugin(TEXT("Raft"));
	if (RaftPlugin.IsValid())
	{
		UGameplayTagsManager::Get().AddTagIniSearchPath(
			FPaths::Combine(RaftPlugin->GetBaseDir(), TEXT("Config/Tags")));
	}
	else
	{
		UE_LOG(LogRaft, Error, TEXT("Unable to register Raft GameplayTags: plugin was not found"));
	}
}

void FRaftRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaftRuntimeModule, RaftRuntime)
