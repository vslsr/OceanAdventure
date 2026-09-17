// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Script/OceanAdventureScriptTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "OceanAdventureScriptHooks.generated.h"

/** Bound from TypeScript. Returns what should actually happen to this hit. */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(
	FOceanAdventureDamageVerdict, FOceanAdventureDamageRuleDelegate, const FOceanAdventureDamageContext&, Context);

/** Bound from TypeScript. Returns whether this interaction may proceed, and how it feels. */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(
	FOceanAdventureInteractionVerdict, FOceanAdventureInteractionRuleDelegate, const FOceanAdventureInteractionContext&, Context);

/**
 * The typed half of the boundary: the places where C++ asks a script to decide something.
 *
 * Four rules govern this class, and each exists because of a way the pattern fails:
 *
 *  1. Every hook has a C++ answer of its own. A rule that is not bound -- no PuerTS installed,
 *     a bundle that failed to parse, a designer mid-edit -- must leave the game playing exactly
 *     as it did before scripts existed. Gameplay that silently stops resolving damage is much
 *     worse than gameplay that ignores a script.
 *  2. Damage is server only; interaction is not. A damage rule decides an authoritative
 *     outcome, so a client running one would be deciding its own hits, and ResolveDamage
 *     refuses off the server. An interaction is a request that the client also runs to
 *     predict with and the server re-checks, so ResolveInteraction runs on both -- gating it
 *     would turn every refusal into a snap-back.
 *  3. Bindings die with the VM. They are closures; a reload builds new ones, and calling a
 *     stale one reaches into a destroyed isolate. The subsystem drops them itself on the
 *     host's stopping delegate, so a script author cannot forget to.
 *  4. The rules are delegate *properties*, bound as `hooks.DamageRule.Bind(fn)`, not arguments
 *     to a setter. Handing a delegate to a UFUNCTION is the part of the script boundary whose
 *     marshalling differs most between VM releases; subscribing to a property is the one form
 *     they all agree on. SetDamageRule() remains for Blueprint and C++ callers.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureScriptHooks : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience for scripts: the hooks of the world a given actor lives in. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script", meta = (WorldContext = "WorldContextObject"))
	static UOceanAdventureScriptHooks* Get(const UObject* WorldContextObject);

	/** Decides the damage of a confirmed hit. Bind from script: DamageRule.Bind(fn). */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FOceanAdventureDamageRuleDelegate DamageRule;

	/** Decides whether an interaction proceeds. Bind from script: InteractionRule.Bind(fn). */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FOceanAdventureInteractionRuleDelegate InteractionRule;

	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script")
	void SetDamageRule(FOceanAdventureDamageRuleDelegate Rule);

	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script")
	void ClearDamageRule();

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script")
	bool HasDamageRule() const { return DamageRule.IsBound(); }

	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script")
	void SetInteractionRule(FOceanAdventureInteractionRuleDelegate Rule);

	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script")
	void ClearInteractionRule();

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script")
	bool HasInteractionRule() const { return InteractionRule.IsBound(); }

	/** True on a server world. Scripts read it to avoid binding rules they are not allowed. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script")
	bool IsAuthority() const;

	/**
	 * Asks the bound rule what this hit does, falling back to the unmodified base damage.
	 *
	 * Callers use the verdict; they do not re-apply BaseDamage afterwards. The clamp to
	 * non-negative lives here so that no caller has to remember it.
	 */
	FOceanAdventureDamageVerdict ResolveDamage(const FOceanAdventureDamageContext& Context) const;

	/** Asks the bound rule whether this interaction proceeds; allows it when none is bound. */
	FOceanAdventureInteractionVerdict ResolveInteraction(const FOceanAdventureInteractionContext& Context) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void OnScriptEnvStopping();

	FDelegateHandle ScriptStoppingHandle;
};
