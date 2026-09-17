// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/TimerHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ScriptEnvSubsystem.generated.h"

class FScriptBackend;
class UScriptEnvSubsystem;
class UScriptEventBus;

/** Why the VM was (re)started, so logs and listeners can tell a boot from a hot reload. */
UENUM(BlueprintType)
enum class EScriptStartReason : uint8
{
	/** A map finished loading, which is when this environment's world exists. */
	Boot,
	/** A file under a script root changed. */
	HotReload,
	/** Somebody asked for it: console command, editor button, test. */
	Manual
};

/**
 * Fired immediately before a script VM is torn down.
 *
 * Every native holder of a script delegate must drop it here. A delegate bound to a closure
 * from the previous VM does not merely stop working after a reload -- invoking it reaches into
 * a destroyed isolate.
 */
DECLARE_MULTICAST_DELEGATE(FOnScriptEnvStopping);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnScriptEnvStarted, EScriptStartReason /*Reason*/);

/**
 * Owns the TypeScript VM for one game instance.
 *
 * The whole point of this subsystem is the sentence "changing a gameplay rule costs a file
 * save, not a rebuild". What that buys, and where it stops, is worth being precise about:
 *
 *  - Rewriting the body of a rule that already exists -- damage curves, refusal conditions,
 *    which cue plays -- needs `npm run build` and, in the editor, not even a PIE restart.
 *  - Reaching something the scripts cannot reach yet needs a new UFUNCTION in the gameplay
 *    layer, and that does need a C++ build. Scripts call down through reflection; they cannot
 *    invent an interface that does not exist.
 *
 * Every script root is discovered, never configured: the project's Content/Script plus the
 * Content/Script of every enabled plugin. A GameFeature therefore ships its scripts inside
 * itself, like the rest of its content, and nothing central has to be edited to add one.
 */
UCLASS()
class SCRIPTCORERUNTIME_API UScriptEnvSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Declared, not defaulted inline: Backend is a TUniquePtr to a forward-declared type, and
	// the destructor UHT generates alongside this header would have to see the full type to
	// destroy it. Defining it in the .cpp, where the backend is complete, is the usual fix.
	UScriptEnvSubsystem();
	virtual ~UScriptEnvSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * The environment whose VM is currently executing, for scripts that need the host itself.
	 *
	 * Valid while native code is inside the VM -- which covers the boot script and every
	 * handler the VM invokes in response to a native call. Outside that window it falls back
	 * to the last environment that booted, and says so when more than one is alive, because
	 * two game instances in one process (a multi-client PIE session) each own a VM and there
	 * is no honest answer to "which one" from a call with no context.
	 */
	UFUNCTION(BlueprintPure, Category = "Script", meta = (DisplayName = "Get Script Environment"))
	static UScriptEnvSubsystem* GetCurrent();

	/** The JSON channel bus. Scripts subscribe here at boot. */
	UFUNCTION(BlueprintPure, Category = "Script")
	UScriptEventBus* GetEventBus() const { return EventBus; }

	UFUNCTION(BlueprintCallable, Category = "Script")
	bool StartScripts(EScriptStartReason Reason = EScriptStartReason::Manual);

	UFUNCTION(BlueprintCallable, Category = "Script")
	void StopScripts();

	/** Stop, re-read every bundle from disk, start again. */
	UFUNCTION(BlueprintCallable, Category = "Script")
	bool ReloadScripts();

	UFUNCTION(BlueprintPure, Category = "Script")
	bool IsRunning() const;

	/** "PuerTS" when a VM is compiled in, "Null" when this build has none. */
	UFUNCTION(BlueprintPure, Category = "Script")
	FString GetBackendName() const;

	/** Absolute at runtime, derived from the plugin manager; nothing here is ever committed. */
	UFUNCTION(BlueprintPure, Category = "Script")
	TArray<FString> GetScriptRoots() const { return ScriptRoots; }

	UFUNCTION(BlueprintPure, Category = "Script")
	TArray<FString> GetLoadedBundles() const { return LoadedBundles; }

	/** Runs a snippet in the live VM; backs `script.eval`. */
	bool Eval(const FString& Source, FString& OutError);

	/** Dropped here by every native holder of a script delegate. See FOnScriptEnvStopping. */
	FOnScriptEnvStopping& OnScriptEnvStopping() { return ScriptEnvStopping; }

	FOnScriptEnvStarted& OnScriptEnvStarted() { return ScriptEnvStarted; }

	/**
	 * Sets the environment native code is currently inside, and returns the previous one.
	 *
	 * Not for gameplay code: use FScopedScriptCall, which pairs the two halves. Public only
	 * because that guard is a plain struct rather than a member of this reflected class.
	 */
	static UScriptEnvSubsystem* SetExecutingEnv(UScriptEnvSubsystem* Env);

private:
	void OnPostLoadMap(UWorld* LoadedWorld);

	void DiscoverScriptRoots();
	FString ReadBundles(TArray<FString>& OutBundleFiles) const;

	void StartWatchingRoots();
	void StopWatchingRoots();

#if WITH_EDITOR
	void OnScriptFilesChanged(const TArray<struct FFileChangeData>& Changes);
#endif

	UPROPERTY()
	TObjectPtr<UScriptEventBus> EventBus;

	TUniquePtr<FScriptBackend> Backend;

	TArray<FString> ScriptRoots;
	TArray<FString> LoadedBundles;

	FOnScriptEnvStopping ScriptEnvStopping;
	FOnScriptEnvStarted ScriptEnvStarted;

	/** Handles of the directory watchers, keyed by the root they watch. */
	TMap<FString, FDelegateHandle> WatcherHandles;

	/** Pending debounced reload; a `tsc` emit writes a burst of files, not one. */
	FTimerHandle HotReloadTimer;

	FDelegateHandle PostLoadMapHandle;

	/** The environment native code is currently inside, and the one that booted last. */
	static TWeakObjectPtr<UScriptEnvSubsystem> ExecutingEnv;
	static TWeakObjectPtr<UScriptEnvSubsystem> LastBootedEnv;
	static int32 LiveEnvCount;
};

/**
 * Marks native code as being inside the VM, which is what makes GetCurrent() exact.
 *
 * Anything that calls into a script delegate should hold one of these, so a script that calls
 * back into the host during that call resolves to the environment that invoked it rather than
 * to whichever one booted last.
 *
 * A free struct rather than a member of the subsystem: UHT parses reflected class bodies, and
 * a nested type in one is a needless thing to ask it to tolerate.
 */
struct SCRIPTCORERUNTIME_API FScopedScriptCall
{
	explicit FScopedScriptCall(UScriptEnvSubsystem* Env);
	~FScopedScriptCall();

	FScopedScriptCall(const FScopedScriptCall&) = delete;
	FScopedScriptCall& operator=(const FScopedScriptCall&) = delete;

private:
	TWeakObjectPtr<UScriptEnvSubsystem> Previous;
};
