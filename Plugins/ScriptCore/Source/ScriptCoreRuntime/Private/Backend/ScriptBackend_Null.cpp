// Copyright Epic Games, Inc. All Rights Reserved.

#include "Backend/ScriptBackend.h"

#include "ScriptCoreRuntimeModule.h"

#if !WITH_SCRIPTCORE_PUERTS

/**
 * What the host gets when no script VM is installed.
 *
 * It deliberately fails loudly instead of silently doing nothing. A missing VM makes every
 * scripted rule fall back to its C++ default, and a fallback that behaves plausibly is the
 * worst possible failure mode: the game plays, the numbers are merely wrong, and the person
 * balancing them edits TypeScript for an hour wondering why nothing changes.
 */
class FScriptBackendNull final : public FScriptBackend
{
public:
	virtual FString GetName() const override { return TEXT("Null"); }
	virtual bool IsFunctional() const override { return false; }

	virtual bool Start(const FScriptBootParams& Boot, FString& OutError) override
	{
		OutError = FString::Printf(
			TEXT("No script VM in this build: PuerTS was not found when ScriptCoreRuntime was compiled. ")
			TEXT("%d script bundle(s) were found and none of them will run. ")
			TEXT("Install the plugin (Tools/setup-puerts.mjs prints how) and rebuild ScriptCoreRuntime."),
			Boot.BundleFiles.Num());
		return false;
	}

	virtual void Stop() override {}
	virtual bool IsRunning() const override { return false; }

	virtual bool Eval(const FString& /*Source*/, FString& OutError) override
	{
		OutError = TEXT("No script VM in this build.");
		return false;
	}
};

TUniquePtr<FScriptBackend> FScriptBackend::Create()
{
	return MakeUnique<FScriptBackendNull>();
}

#endif // !WITH_SCRIPTCORE_PUERTS
