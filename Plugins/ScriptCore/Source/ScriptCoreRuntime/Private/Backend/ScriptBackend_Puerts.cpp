// Copyright Epic Games, Inc. All Rights Reserved.

#include "Backend/ScriptBackend.h"

#include "ScriptCoreRuntimeModule.h"

#if WITH_SCRIPTCORE_PUERTS

#include "JsEnv.h"

/**
 * The PuerTS backend -- and the only file in this repository that names a PuerTS type.
 *
 * PuerTS is installed per machine rather than committed, so its exact entry points can differ
 * by release. That is why the surface used here is kept to three calls: construct FJsEnv with
 * a script root, Start() a source string, destroy. Everything else the host needs -- module
 * resolution, hot reload, the event channel -- is solved on the Unreal side where it can be
 * read and fixed without a V8 build. If a future PuerTS changes a signature, this file is the
 * whole blast radius; nothing else has to be touched.
 *
 * Not wired on purpose: the V8 inspector. It needs FJsEnv's extended constructor, which also
 * wants a module loader and a logger, and those are the two pieces whose shape varies most
 * between releases. doc/tech/TypeScript-PuerTS.md records how to turn it on deliberately.
 */
class FScriptBackendPuerts final : public FScriptBackend
{
public:
	virtual ~FScriptBackendPuerts() override
	{
		Stop();
	}

	virtual FString GetName() const override { return TEXT("PuerTS"); }
	virtual bool IsFunctional() const override { return true; }

	virtual bool Start(const FScriptBootParams& Boot, FString& OutError) override
	{
		if (Boot.Source.IsEmpty())
		{
			OutError = TEXT("Nothing to run: no script bundle was found under any script root.");
			return false;
		}

		Stop();

		JsEnv = MakeShared<puerts::FJsEnv>(Boot.PrimaryScriptRoot);

		// IsScript=true: the argument is the program text, not a module name. The host already
		// read and concatenated the bundles, so there is no resolution step to get wrong.
		JsEnv->Start(Boot.Source, {}, /*IsScript=*/true);

		return true;
	}

	virtual void Stop() override
	{
		// Releasing the env tears down the isolate, which drops every JS closure still holding
		// a UObject. Handlers on the C++ side are swept separately by the host before this
		// runs -- see UScriptEnvSubsystem::StopScripts -- because an already-dead closure
		// invoked through a stale delegate is a crash, not a no-op.
		JsEnv.Reset();
	}

	virtual bool IsRunning() const override
	{
		return JsEnv.IsValid();
	}

	virtual bool Eval(const FString& Source, FString& OutError) override
	{
		if (!JsEnv.IsValid())
		{
			OutError = TEXT("Script VM is not running.");
			return false;
		}

		JsEnv->Start(Source, {}, /*IsScript=*/true);
		return true;
	}

private:
	TSharedPtr<puerts::FJsEnv> JsEnv;
};

TUniquePtr<FScriptBackend> FScriptBackend::Create()
{
	return MakeUnique<FScriptBackendPuerts>();
}

#endif // WITH_SCRIPTCORE_PUERTS
