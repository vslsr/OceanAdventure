// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalDamageRelay.h"

#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "Script/OceanAdventureCombatScriptLibrary.h"
#include "Script/OceanAdventureFeedbackScriptLibrary.h"
#include "Script/OceanAdventureScriptHooks.h"
#include "Script/OceanAdventureScriptTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalDamageRelay)

bool UOceanAdventureNavalDamageRelay::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOceanAdventureNavalDamageRelay::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ImpactListenerHandle = UGameplayMessageSubsystem::Get(World)
		.RegisterListener<FNavalProjectileImpactMessage>(
			NavalGameplayTags::Message_Projectile_Impact,
			this,
			&UOceanAdventureNavalDamageRelay::OnProjectileImpact);
}

void UOceanAdventureNavalDamageRelay::Deinitialize()
{
	// The game-instance message subsystem may already be gone while world subsystems are
	// tearing down. The handle owns a weak reference to the router and safely becomes a no-op
	// in that case; calling UGameplayMessageSubsystem::Get here would assert on a null router.
	ImpactListenerHandle.Unregister();
	ImpactListenerHandle = FGameplayMessageListenerHandle();

	Super::Deinitialize();
}

void UOceanAdventureNavalDamageRelay::OnProjectileImpact(
	FGameplayTag /*Channel*/, const FNavalProjectileImpactMessage& Message)
{
	if (Message.PendingCharacterDamage <= 0.0f || !Message.HitActor)
	{
		return;
	}

	// The scripted damage rule sits exactly here: after the framework has resolved *whether*
	// and *where* a shot landed, before GAS is told how much it hurt. That split is what makes
	// the rule safe to hot reload -- ballistics, authority and the wall-and-window rule stay in
	// C++ where they are reviewed, and only the arithmetic on top of a confirmed hit moves.
	const FOceanAdventureDamageContext Context = UOceanAdventureCombatScriptLibrary::MakeDamageContext(
		Message.Instigator,
		Message.HitActor,
		Message.Projectile,
		Message.PendingCharacterDamage,
		Message.ImpactLocation,
		Message.ImpactNormal,
		OceanAdventureNavalTags::SetByCaller_Naval_Damage);

	FOceanAdventureDamageVerdict Verdict;
	Verdict.Damage = Message.PendingCharacterDamage;

	if (const UOceanAdventureScriptHooks* Hooks = UOceanAdventureScriptHooks::Get(this))
	{
		// With no rule bound -- no script VM, a bundle that failed to parse, a designer
		// mid-edit -- this returns the base damage unchanged, so the loop behaves exactly as
		// it did before scripting existed.
		Verdict = Hooks->ResolveDamage(Context);
	}

	if (Verdict.bCancelled)
	{
		UE_LOG(
			LogOceanAdventure,
			Verbose,
			TEXT("[NavalDamage] A script cancelled %.1f damage to %s (%s)"),
			Message.PendingCharacterDamage,
			*GetNameSafe(Message.HitActor),
			*Verdict.ReasonTag.ToString());
		return;
	}

	if (Verdict.ImpactCueTag.IsValid())
	{
		UOceanAdventureFeedbackScriptLibrary::PlayCueOnActor(
			Message.HitActor, Verdict.ImpactCueTag, Message.ImpactLocation, Message.ImpactNormal);
	}

	UOceanAdventureCombatScriptLibrary::ApplyCharacterDamage(
		Message.HitActor,
		Message.Instigator,
		Message.Projectile,
		Verdict.Damage,
		Message.ImpactLocation);
}
