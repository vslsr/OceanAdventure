// Copyright Epic Games, Inc. All Rights Reserved.

#include "RaftRuntimeModule.h"

#define LOCTEXT_NAMESPACE "FRaftRuntimeModule"

DEFINE_LOG_CATEGORY(LogRaft);

void FRaftRuntimeModule::StartupModule()
{
}

void FRaftRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaftRuntimeModule, RaftRuntime)
