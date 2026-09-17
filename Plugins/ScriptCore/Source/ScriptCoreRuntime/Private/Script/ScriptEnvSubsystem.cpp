// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/ScriptEnvSubsystem.h"

#include "Backend/ScriptBackend.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScriptCoreRuntimeModule.h"
#include "Script/ScriptEventBus.h"
#include "Script/ScriptHostSettings.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptEnvSubsystem)

TWeakObjectPtr<UScriptEnvSubsystem> UScriptEnvSubsystem::ExecutingEnv;
TWeakObjectPtr<UScriptEnvSubsystem> UScriptEnvSubsystem::LastBootedEnv;
int32 UScriptEnvSubsystem::LiveEnvCount = 0;

namespace ScriptEnvPrivate
{
	/** Folder every script root ends in, under a plugin's or the project's Content. */
	static const TCHAR* ScriptFolderName = TEXT("Script");

	/**
	 * Quotes a path for embedding in generated JavaScript.
	 *
	 * Windows paths are full of backslashes, and an unescaped one turns the next character
	 * into an escape sequence -- so a perfectly good bundle would fail to parse, at boot,
	 * with a syntax error pointing at a line the reader never wrote.
	 */
	static FString EscapeForJsString(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
		return Out;
	}
}

UScriptEnvSubsystem::UScriptEnvSubsystem() = default;

// Defined here rather than in the header: Backend is a TUniquePtr to a forward-declared type,
// and destroying one needs the complete type, which only this translation unit has.
UScriptEnvSubsystem::~UScriptEnvSubsystem() = default;

UScriptEnvSubsystem* UScriptEnvSubsystem::SetExecutingEnv(UScriptEnvSubsystem* Env)
{
	UScriptEnvSubsystem* Previous = ExecutingEnv.Get();
	ExecutingEnv = Env;
	return Previous;
}

FScopedScriptCall::FScopedScriptCall(UScriptEnvSubsystem* Env)
{
	Previous = UScriptEnvSubsystem::SetExecutingEnv(Env);
}

FScopedScriptCall::~FScopedScriptCall()
{
	UScriptEnvSubsystem::SetExecutingEnv(Previous.Get());
}

void UScriptEnvSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EventBus = NewObject<UScriptEventBus>(this, TEXT("ScriptEventBus"));
	Backend = FScriptBackend::Create();

	++LiveEnvCount;

	// Scripts boot on map load, not here. A game instance subsystem initialises before any
	// world exists, and almost everything a script binds to -- the damage hook, the message
	// bridge -- is a world subsystem. Booting here would run every bundle against a null
	// world, and the symptom would be handlers that simply never fire, with no error anywhere.
	// Restarting on each load is also what rebinds scripts after a travel, since the previous
	// world's subsystems (and the delegates pointing at them) went away with it.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UScriptEnvSubsystem::OnPostLoadMap);
}

void UScriptEnvSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (!LoadedWorld || !GameInstance || LoadedWorld->GetGameInstance() != GameInstance)
	{
		// Somebody else's world. In a multi-client PIE session this delegate fires once per
		// game instance, and each environment must only answer for its own.
		return;
	}

	if (UScriptHostSettings::Get().bAutoStart)
	{
		StartScripts(EScriptStartReason::Boot);
	}
}

void UScriptEnvSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	StopWatchingRoots();
	StopScripts();

	Backend.Reset();

	if (ExecutingEnv.Get() == this)
	{
		ExecutingEnv.Reset();
	}
	if (LastBootedEnv.Get() == this)
	{
		LastBootedEnv.Reset();
	}

	--LiveEnvCount;

	Super::Deinitialize();
}

UScriptEnvSubsystem* UScriptEnvSubsystem::GetCurrent()
{
	if (UScriptEnvSubsystem* Executing = ExecutingEnv.Get())
	{
		return Executing;
	}

	if (LiveEnvCount > 1)
	{
		// One process, several game instances -- a multi-client PIE session. Each owns its own
		// VM, and a call arriving from outside any of them carries nothing to tell them apart.
		// Answering with the last one that booted is a guess, so it is logged as one.
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(
				LogScriptCore,
				Warning,
				TEXT("[Script] %d script environments are live; a script call made outside a host call ")
				TEXT("resolves to the one that booted last. Run multi-client tests as separate processes."),
				LiveEnvCount);
		}
	}

	return LastBootedEnv.Get();
}

bool UScriptEnvSubsystem::IsRunning() const
{
	return Backend.IsValid() && Backend->IsRunning();
}

FString UScriptEnvSubsystem::GetBackendName() const
{
	return Backend.IsValid() ? Backend->GetName() : TEXT("None");
}

void UScriptEnvSubsystem::DiscoverScriptRoots()
{
	ScriptRoots.Reset();

	auto AddRootIfPresent = [this](const FString& ContentDir)
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(ContentDir, ScriptEnvPrivate::ScriptFolderName));
		if (IFileManager::Get().DirectoryExists(*Root))
		{
			ScriptRoots.AddUnique(Root);
		}
	};

	AddRootIfPresent(FPaths::ProjectContentDir());

	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		AddRootIfPresent(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content")));
	}

	for (const FString& Extra : UScriptHostSettings::Get().AdditionalScriptRoots)
	{
		// Relative to the project, never to a machine: a configured root has to survive the
		// checkout being cloned somewhere else.
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Extra));
		if (IFileManager::Get().DirectoryExists(*Root))
		{
			ScriptRoots.AddUnique(Root);
		}
		else
		{
			UE_LOG(LogScriptCore, Warning, TEXT("[Script] Configured script root does not exist: %s"), *Root);
		}
	}

	// Stable order, so the concatenated program is byte-identical between runs and between
	// machines. Bundles are independent, but a boot failure that moves around by platform is
	// far harder to read than one that always reports the same bundle first.
	ScriptRoots.Sort();
}

FString UScriptEnvSubsystem::ReadBundles(TArray<FString>& OutBundleFiles) const
{
	OutBundleFiles.Reset();

	const FString EntryFile = UScriptHostSettings::Get().EntryModule + TEXT(".js");

	FString Program;

	for (const FString& Root : ScriptRoots)
	{
		const FString BundlePath = FPaths::Combine(Root, EntryFile);
		if (!IFileManager::Get().FileExists(*BundlePath))
		{
			continue;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *BundlePath))
		{
			UE_LOG(LogScriptCore, Error, TEXT("[Script] Could not read bundle: %s"), *BundlePath);
			continue;
		}

		const FString Escaped = ScriptEnvPrivate::EscapeForJsString(BundlePath);

		// One try/catch per bundle: a feature whose script throws on load must not take the
		// other features' scripts down with it. Each bundle is a self-contained IIFE, so the
		// only thing the wrapper changes is who hears about the failure.
		Program += FString::Printf(TEXT("\n/* --- %s --- */\ntry {\n"), *Escaped);
		Program += Contents;
		Program += FString::Printf(
			TEXT("\n} catch (e) {\n")
			TEXT("  if (typeof console !== 'undefined' && console.error) {\n")
			TEXT("    console.error('[ScriptCore] bundle failed to load: %s', (e && e.stack) || e);\n")
			TEXT("  }\n")
			TEXT("}\n"),
			*Escaped);

		OutBundleFiles.Add(BundlePath);
	}

	return Program;
}

bool UScriptEnvSubsystem::StartScripts(EScriptStartReason Reason)
{
	if (!Backend.IsValid())
	{
		return false;
	}

	if (Backend->IsRunning())
	{
		StopScripts();
	}

	DiscoverScriptRoots();
	StartWatchingRoots();

	FScriptBootParams Boot;
	Boot.Source = ReadBundles(Boot.BundleFiles);
	Boot.PrimaryScriptRoot = ScriptRoots.Num() > 0 ? ScriptRoots[0] : FPaths::ProjectContentDir();

	LoadedBundles = Boot.BundleFiles;

	if (Boot.BundleFiles.Num() == 0)
	{
		UE_LOG(
			LogScriptCore,
			Warning,
			TEXT("[Script] No '%s.js' found under %d script root(s); nothing to run. ")
			TEXT("Run `npm run build` in the TypeScript project that owns the rules you expect."),
			*UScriptHostSettings::Get().EntryModule,
			ScriptRoots.Num());
		return false;
	}

	LastBootedEnv = this;

	FString Error;
	bool bStarted = false;
	{
		// The boot program registers its handlers while this guard is held, so a script that
		// asks for its host during registration gets this environment and not another one.
		FScopedScriptCall ScopedCall(this);
		bStarted = Backend->Start(Boot, Error);
	}

	if (!bStarted)
	{
		UE_LOG(LogScriptCore, Error, TEXT("[Script] Start failed: %s"), *Error);
		return false;
	}

	UE_LOG(
		LogScriptCore,
		Log,
		TEXT("[Script] %s started %d bundle(s) from %d root(s) (%s)"),
		*Backend->GetName(),
		Boot.BundleFiles.Num(),
		ScriptRoots.Num(),
		Reason == EScriptStartReason::HotReload ? TEXT("hot reload")
			: Reason == EScriptStartReason::Boot ? TEXT("boot")
			: TEXT("manual"));

	ScriptEnvStarted.Broadcast(Reason);

	return true;
}

void UScriptEnvSubsystem::StopScripts()
{
	if (!Backend.IsValid() || !Backend->IsRunning())
	{
		return;
	}

	// Order matters and is the reason this is not two lines in the caller. Native holders drop
	// their script delegates first, the bus drops the rest, and only then does the isolate go
	// away. Reversed, a delegate left pointing into a destroyed isolate is a crash the next
	// time anything fires -- typically one frame later, nowhere near the reload.
	ScriptEnvStopping.Broadcast();

	if (EventBus)
	{
		EventBus->ClearScriptHandlers();
	}

	Backend->Stop();
	LoadedBundles.Reset();
}

bool UScriptEnvSubsystem::ReloadScripts()
{
	StopScripts();
	return StartScripts(EScriptStartReason::HotReload);
}

bool UScriptEnvSubsystem::Eval(const FString& Source, FString& OutError)
{
	if (!Backend.IsValid())
	{
		OutError = TEXT("No script backend.");
		return false;
	}

	FScopedScriptCall ScopedCall(this);
	return Backend->Eval(Source, OutError);
}

void UScriptEnvSubsystem::StartWatchingRoots()
{
#if WITH_EDITOR
	if (!UScriptHostSettings::Get().bEnableHotReload)
	{
		return;
	}

	FDirectoryWatcherModule& WatcherModule =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = WatcherModule.Get();
	if (!Watcher)
	{
		return;
	}

	for (const FString& Root : ScriptRoots)
	{
		if (WatcherHandles.Contains(Root))
		{
			continue;
		}

		FDelegateHandle Handle;
		if (Watcher->RegisterDirectoryChangedCallback_Handle(
				Root,
				IDirectoryWatcher::FDirectoryChanged::CreateUObject(this, &UScriptEnvSubsystem::OnScriptFilesChanged),
				Handle,
				IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
		{
			WatcherHandles.Add(Root, Handle);
		}
	}
#endif
}

void UScriptEnvSubsystem::StopWatchingRoots()
{
#if WITH_EDITOR
	if (WatcherHandles.Num() == 0)
	{
		return;
	}

	if (FDirectoryWatcherModule* WatcherModule =
			FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = WatcherModule->Get())
		{
			for (const TPair<FString, FDelegateHandle>& Pair : WatcherHandles)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Pair.Key, Pair.Value);
			}
		}
	}

	WatcherHandles.Reset();
#endif
}

#if WITH_EDITOR
void UScriptEnvSubsystem::OnScriptFilesChanged(const TArray<FFileChangeData>& Changes)
{
	const bool bTouchedScript = Changes.ContainsByPredicate(
		[](const FFileChangeData& Change)
		{
			return FPaths::GetExtension(Change.Filename).Equals(TEXT("js"), ESearchCase::IgnoreCase);
		});

	if (!bTouchedScript)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GameInstance->GetWorld())
	{
		return;
	}

	// Debounced: one `tsc` emit writes several files in a burst, and reloading on the first of
	// them runs a dist that is still half-written -- which shows up as a random syntax error
	// in a file the author had just finished fixing.
	FTimerManager& Timers = GameInstance->GetTimerManager();
	Timers.ClearTimer(HotReloadTimer);
	Timers.SetTimer(
		HotReloadTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { ReloadScripts(); }),
		FMath::Max(UScriptHostSettings::Get().HotReloadDebounceSeconds, 0.01f),
		/*bLoop=*/false);
}
#endif

#if !UE_BUILD_SHIPPING

namespace ScriptConsole
{
	static UScriptEnvSubsystem* ResolveEnv(UWorld* World)
	{
		if (World)
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				return GameInstance->GetSubsystem<UScriptEnvSubsystem>();
			}
		}

		return UScriptEnvSubsystem::GetCurrent();
	}

	static FAutoConsoleCommandWithWorldAndArgs ReloadCommand(
		TEXT("script.reload"),
		TEXT("Re-read every script bundle from disk and restart the script VM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UScriptEnvSubsystem* Env = ResolveEnv(World))
				{
					UE_LOG(LogScriptCore, Log, TEXT("[Script] Reload requested from console: %s"),
						Env->ReloadScripts() ? TEXT("ok") : TEXT("failed"));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs StatusCommand(
		TEXT("script.status"),
		TEXT("Print the script backend, roots and loaded bundles."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				UScriptEnvSubsystem* Env = ResolveEnv(World);
				if (!Env)
				{
					UE_LOG(LogScriptCore, Log, TEXT("[Script] No script environment in this world."));
					return;
				}

				UE_LOG(LogScriptCore, Log, TEXT("[Script] backend=%s running=%s"),
					*Env->GetBackendName(), Env->IsRunning() ? TEXT("yes") : TEXT("no"));

				for (const FString& Root : Env->GetScriptRoots())
				{
					UE_LOG(LogScriptCore, Log, TEXT("[Script]   root   %s"), *Root);
				}
				for (const FString& Bundle : Env->GetLoadedBundles())
				{
					UE_LOG(LogScriptCore, Log, TEXT("[Script]   bundle %s"), *Bundle);
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs EvalCommand(
		TEXT("script.eval"),
		TEXT("Run a snippet in the live script VM, e.g. script.eval console.log(1+1)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UScriptEnvSubsystem* Env = ResolveEnv(World);
				if (!Env)
				{
					return;
				}

				const FString Source = FString::Join(Args, TEXT(" "));
				FString Error;
				if (!Env->Eval(Source, Error))
				{
					UE_LOG(LogScriptCore, Error, TEXT("[Script] eval failed: %s"), *Error);
				}
			}));
}

#endif // !UE_BUILD_SHIPPING
