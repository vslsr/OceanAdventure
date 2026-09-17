// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

/** Everything the host hands a VM at boot. */
struct FScriptBootParams
{
	/**
	 * The full source to run: every discovered bundle, concatenated.
	 *
	 * Bundles rather than modules, and source rather than a module name, on purpose. Each
	 * GameFeature builds one self-contained IIFE, so no module resolver has to be taught where
	 * a feature's Content/Script lives, and -- more importantly -- two features can never end
	 * up importing each other's TypeScript. The repository forbids that dependency in C++ and
	 * in assets; letting it in through a `require` would be the same mistake with a new syntax.
	 */
	FString Source;

	/** Where the bundles came from, for logs and for the hot-reload watcher. */
	TArray<FString> BundleFiles;

	/** Root folder handed to the VM for its own built-in resolution. */
	FString PrimaryScriptRoot;
};

/**
 * The seam between the host and whichever script VM is installed.
 *
 * Only one implementation is compiled into a given build: the PuerTS one when the plugin is
 * present, the null one when it is not. The null backend exists so that a fresh checkout --
 * which has no PuerTS, because it is far too large for this repository -- still builds, still
 * opens and still plays with the C++ defaults, and says exactly once why no script ran.
 */
class FScriptBackend
{
public:
	virtual ~FScriptBackend() = default;

	/** Name for logs, e.g. "PuerTS" or "Null". */
	virtual FString GetName() const = 0;

	/** True when a real VM is behind this backend. */
	virtual bool IsFunctional() const = 0;

	virtual bool Start(const FScriptBootParams& Boot, FString& OutError) = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() const = 0;

	/** Runs a snippet in the live VM. Backs the `script.eval` console command. */
	virtual bool Eval(const FString& Source, FString& OutError) = 0;

	/** Built by the backend that this build was compiled with. */
	static TUniquePtr<FScriptBackend> Create();
};
