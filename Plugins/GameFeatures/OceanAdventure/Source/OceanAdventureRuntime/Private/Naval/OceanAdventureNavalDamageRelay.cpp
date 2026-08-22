// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalDamageRelay.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayEffect.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/OceanAdventureNavalSettings.h"
#include "OceanAdventureRuntimeModule.h"

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
	if (const UWorld* World = GetWorld(); World && ImpactListenerHandle.IsValid())
	{
		UGameplayMessageSubsystem::Get(World).UnregisterListener(ImpactListenerHandle);
	}
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

	UAbilitySystemComponent* TargetAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Message.HitActor);
	if (!TargetAbilitySystem)
	{
		return;
	}

	const UOceanAdventureNavalSettings& Settings = UOceanAdventureNavalSettings::Get();
	const TSubclassOf<UGameplayEffect> DamageEffect = Settings.ProjectileDamageEffect.LoadSynchronous();
	if (!DamageEffect)
	{
		// Loud rather than silent: a project that forgets to configure this would otherwise
		// ship heavy weapons that cannot hurt anybody, and nothing would say so.
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[NavalDamage] No ProjectileDamageEffect configured; character damage from %s was dropped"),
			*GetNameSafe(Message.Projectile));
		return;
	}

	UAbilitySystemComponent* SourceAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Message.Instigator);
	UAbilitySystemComponent* EffectSource = SourceAbilitySystem ? SourceAbilitySystem : TargetAbilitySystem;

	FGameplayEffectContextHandle ContextHandle = EffectSource->MakeEffectContext();
	ContextHandle.AddInstigator(Message.Instigator, Message.Projectile);
	ContextHandle.AddOrigin(Message.ImpactLocation);

	const FGameplayEffectSpecHandle SpecHandle =
		EffectSource->MakeOutgoingSpec(DamageEffect, /*Level=*/1.0f, ContextHandle);
	if (!SpecHandle.IsValid())
	{
		return;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(
		Settings.DamageSetByCallerTag, Message.PendingCharacterDamage);
	EffectSource->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data, TargetAbilitySystem);
}
