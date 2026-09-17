// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/OceanAdventureScriptHooks.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OceanAdventureRuntimeModule.h"
#include "Script/ScriptEnvSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureScriptHooks)

namespace OceanAdventureScriptHooksPrivate
{
	static UScriptEnvSubsystem* FindEnv(const UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UScriptEnvSubsystem>() : nullptr;
	}
}

bool UOceanAdventureScriptHooks::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOceanAdventureScriptHooks::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UScriptEnvSubsystem* Env = OceanAdventureScriptHooksPrivate::FindEnv(GetWorld()))
	{
		ScriptStoppingHandle = Env->OnScriptEnvStopping().AddUObject(
			this, &UOceanAdventureScriptHooks::OnScriptEnvStopping);
	}
}

void UOceanAdventureScriptHooks::Deinitialize()
{
	if (ScriptStoppingHandle.IsValid())
	{
		if (UScriptEnvSubsystem* Env = OceanAdventureScriptHooksPrivate::FindEnv(GetWorld()))
		{
			Env->OnScriptEnvStopping().Remove(ScriptStoppingHandle);
		}

		ScriptStoppingHandle.Reset();
	}

	DamageRule.Unbind();
	InteractionRule.Unbind();

	Super::Deinitialize();
}

UOceanAdventureScriptHooks* UOceanAdventureScriptHooks::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UOceanAdventureScriptHooks>() : nullptr;
}

bool UOceanAdventureScriptHooks::IsAuthority() const
{
	const UWorld* World = GetWorld();
	return World && (World->GetNetMode() != NM_Client);
}

void UOceanAdventureScriptHooks::OnScriptEnvStopping()
{
	// The closures behind these are about to stop existing. Unbinding here, rather than hoping
	// the next bundle overwrites them, is what makes a reload with a broken bundle fall back to
	// the C++ defaults instead of calling into a dead isolate.
	DamageRule.Unbind();
	InteractionRule.Unbind();
}

void UOceanAdventureScriptHooks::SetDamageRule(FOceanAdventureDamageRuleDelegate Rule)
{
	DamageRule = Rule;
}

void UOceanAdventureScriptHooks::ClearDamageRule()
{
	DamageRule.Unbind();
}

void UOceanAdventureScriptHooks::SetInteractionRule(FOceanAdventureInteractionRuleDelegate Rule)
{
	InteractionRule = Rule;
}

void UOceanAdventureScriptHooks::ClearInteractionRule()
{
	InteractionRule.Unbind();
}

FOceanAdventureDamageVerdict UOceanAdventureScriptHooks::ResolveDamage(
	const FOceanAdventureDamageContext& Context) const
{
	FOceanAdventureDamageVerdict Verdict;
	Verdict.Damage = Context.BaseDamage;

	if (!DamageRule.IsBound())
	{
		return Verdict;
	}

	if (!IsAuthority())
	{
		// Checked at the call and not only at the bind: a rule can be bound before a listen
		// server has finished deciding what it is, and a client that resolved its own damage
		// would disagree with the server in a way that only shows up as rubber-banding health.
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[Script] A damage rule is bound on a client and was ignored. Damage is server truth."));
		return Verdict;
	}

	{
		// Marks the host as executing, so a rule that calls back into the host resolves to this
		// world's environment rather than to whichever one booted last.
		FScopedScriptCall ScopedCall(
			OceanAdventureScriptHooksPrivate::FindEnv(GetWorld()));

		Verdict = DamageRule.Execute(Context);
	}

	// A rule that returns a negative would otherwise heal whatever it hit. Clamping here means
	// no caller has to defend against it, and no reviewer has to check that they did.
	Verdict.Damage = FMath::Max(Verdict.Damage, 0.0f);

	return Verdict;
}

FOceanAdventureInteractionVerdict UOceanAdventureScriptHooks::ResolveInteraction(
	const FOceanAdventureInteractionContext& Context) const
{
	FOceanAdventureInteractionVerdict Verdict;

	// No authority gate here, unlike ResolveDamage, and the difference is deliberate: an
	// interaction is a *request*, checked on the client to predict with and re-checked by the
	// server before anything happens. Running this rule only on the server would make every
	// predicted interaction it refuses snap back, and a client running a tampered rule can
	// only mislead itself -- the server's own copy still decides.
	if (!InteractionRule.IsBound())
	{
		return Verdict;
	}

	{
		FScopedScriptCall ScopedCall(
			OceanAdventureScriptHooksPrivate::FindEnv(GetWorld()));

		Verdict = InteractionRule.Execute(Context);
	}

	Verdict.DurationScale = FMath::Clamp(Verdict.DurationScale, 0.0f, 10.0f);

	return Verdict;
}
