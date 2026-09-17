// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "ScriptHostSettings.generated.h"

/**
 * Project settings for the TypeScript host.
 *
 * Everything here is deliberately a knob rather than a constant: the whole point of moving
 * gameplay rules into TypeScript is that changing them costs no C++ rebuild, and that only
 * holds if the entry point, the debug port and the hot-reload switch are configuration too.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Script Host (TypeScript)"))
class SCRIPTCORERUNTIME_API UScriptHostSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UScriptHostSettings();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	static const UScriptHostSettings& Get();

	/**
	 * Module the host requires on boot, resolved against every script root.
	 *
	 * "main" means <root>/main.js. A missing entry is a warning and not a failure: a
	 * checkout that has not run `npm run build` yet should still open and play.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Script")
	FString EntryModule = TEXT("main");

	/** Start the script VM automatically each time a map finishes loading. */
	UPROPERTY(Config, EditAnywhere, Category = "Script")
	bool bAutoStart = true;

	/**
	 * Re-run the entry module when a file under a script root changes. Editor only -- this is
	 * the switch that turns "change a damage rule" from a rebuild into a file save.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Script")
	bool bEnableHotReload = true;

	/**
	 * Debounce for the hot-reload watcher. A `tsc` emit writes several files in a burst and
	 * reloading on the first one would run a half-written dist.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Script", meta = (ClampMin = "0.0", Units = "s"))
	float HotReloadDebounceSeconds = 0.4f;

	/**
	 * Extra script roots relative to the project directory, searched after the automatic ones.
	 *
	 * Roots are normally discovered: the project's own Content/Script plus the Content/Script
	 * of every enabled plugin. This is for a checkout that keeps a scratch root elsewhere; the
	 * path stays relative, so it survives being cloned to a different machine.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Script")
	TArray<FString> AdditionalScriptRoots;
};
