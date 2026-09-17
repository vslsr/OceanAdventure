// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/OceanAdventureScriptMessageBridge.h"

#include "Build/OceanAdventureBuildMessages.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Carry/OceanAdventureCarryMessages.h"
#include "Carry/OceanAdventureCarryTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "OceanAdventureRuntimeModule.h"
#include "Script/ScriptEnvSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureScriptMessageBridge)

namespace OceanAdventureScriptBridge
{
	/** Serialises the original message so a script can read fields the flattening drops. */
	template <typename TMessage>
	static FString ToJson(const TMessage& Message)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(
			TMessage::StaticStruct(), &Message, Json, /*CheckFlags=*/0, /*SkipFlags=*/0, /*Indent=*/0);
		return Json;
	}
}

bool UOceanAdventureScriptMessageBridge::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOceanAdventureScriptMessageBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegisterForwarders();

	if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UScriptEnvSubsystem* Env = GameInstance->GetSubsystem<UScriptEnvSubsystem>())
		{
			ScriptStoppingHandle = Env->OnScriptEnvStopping().AddUObject(
				this, &UOceanAdventureScriptMessageBridge::ClearScriptHandlers);
		}
	}
}

void UOceanAdventureScriptMessageBridge::Deinitialize()
{
	if (ScriptStoppingHandle.IsValid())
	{
		if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (UScriptEnvSubsystem* Env = GameInstance->GetSubsystem<UScriptEnvSubsystem>())
			{
				Env->OnScriptEnvStopping().Remove(ScriptStoppingHandle);
			}
		}

		ScriptStoppingHandle.Reset();
	}

	UnregisterForwarders();
	OnGameplayMessage.Clear();

	Super::Deinitialize();
}

UOceanAdventureScriptMessageBridge* UOceanAdventureScriptMessageBridge::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UOceanAdventureScriptMessageBridge>() : nullptr;
}

void UOceanAdventureScriptMessageBridge::ForwardToScripts(const FOceanAdventureScriptMessage& Message)
{
	if (!OnGameplayMessage.IsBound())
	{
		return;
	}

	FScopedScriptCall ScopedCall(
		GetWorld() && GetWorld()->GetGameInstance()
			? GetWorld()->GetGameInstance()->GetSubsystem<UScriptEnvSubsystem>()
			: nullptr);

	OnGameplayMessage.Broadcast(Message);
}

void UOceanAdventureScriptMessageBridge::ClearScriptHandlers()
{
	// Every subscriber here is a script closure from the VM that is about to go away.
	OnGameplayMessage.Clear();
}

void UOceanAdventureScriptMessageBridge::RegisterForwarders()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameplayMessageSubsystem& Router = UGameplayMessageSubsystem::Get(World);

	// One lambda per channel, each naming the fields it flattens. Verbose on purpose: this is
	// the list a script author reads to find out what they can react to, and a reflective
	// walker would have made it unreadable in exchange for saving these lines once.

	ListenerHandles.Add(Router.RegisterListener<FNavalProjectileImpactMessage>(
		NavalGameplayTags::Message_Projectile_Impact,
		[this](FGameplayTag Channel, const FNavalProjectileImpactMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Instigator;
			Out.Target = In.HitActor;
			Out.WorldLocation = In.ImpactLocation;
			Out.Magnitude = In.StructureDamageApplied + In.PendingCharacterDamage;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(NavalGameplayTags::Message_Projectile_Impact);

	ListenerHandles.Add(Router.RegisterListener<FNavalVesselStateMessage>(
		NavalGameplayTags::Message_Vessel_State,
		[this](FGameplayTag Channel, const FNavalVesselStateMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Vessel;
			Out.WorldLocation = In.Vessel ? In.Vessel->GetActorLocation() : FVector::ZeroVector;
			Out.Magnitude = In.HullFraction;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(NavalGameplayTags::Message_Vessel_State);

	ListenerHandles.Add(Router.RegisterListener<FNavalPartStateMessage>(
		NavalGameplayTags::Message_Vessel_Part,
		[this](FGameplayTag Channel, const FNavalPartStateMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Vessel;
			Out.Target = In.PartActor;
			Out.WorldLocation = In.PartActor ? In.PartActor->GetActorLocation() : FVector::ZeroVector;
			Out.Magnitude = In.DurabilityFraction;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(NavalGameplayTags::Message_Vessel_Part);

	ListenerHandles.Add(Router.RegisterListener<FNavalShotBlockedMessage>(
		NavalGameplayTags::Message_Shot_Blocked,
		[this](FGameplayTag Channel, const FNavalShotBlockedMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Instigator;
			Out.Target = In.Blocker;
			Out.WorldLocation = In.ImpactLocation;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(NavalGameplayTags::Message_Shot_Blocked);

	ListenerHandles.Add(Router.RegisterListener<FNavalHeavyWeaponMessage>(
		NavalGameplayTags::Message_HeavyWeapon_State,
		[this](FGameplayTag Channel, const FNavalHeavyWeaponMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Operator;
			Out.Target = In.Weapon;
			Out.WorldLocation = In.Weapon ? In.Weapon->GetActorLocation() : FVector::ZeroVector;
			Out.Magnitude = In.ReloadSecondsRemaining;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(NavalGameplayTags::Message_HeavyWeapon_State);

	ListenerHandles.Add(Router.RegisterListener<FOceanAdventureCarryFailedMessage>(
		OceanAdventureCarryTags::Message_Carry_Failed,
		[this](FGameplayTag Channel, const FOceanAdventureCarryFailedMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.Instigator;
			Out.Target = In.CarryTarget;
			Out.WorldLocation = In.Instigator ? In.Instigator->GetActorLocation() : FVector::ZeroVector;
			Out.ReasonTag = In.FailReason;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(OceanAdventureCarryTags::Message_Carry_Failed);

	ListenerHandles.Add(Router.RegisterListener<FOceanAdventureBuildFailedMessage>(
		OceanAdventureBuildTags::Message_Build_Failed,
		[this](FGameplayTag Channel, const FOceanAdventureBuildFailedMessage& In)
		{
			FOceanAdventureScriptMessage Out;
			Out.Channel = Channel;
			Out.Source = In.HostActor;
			Out.WorldLocation = In.HostActor ? In.HostActor->GetActorLocation() : FVector::ZeroVector;
			Out.ReasonTag = In.FailReason;
			Out.PayloadJson = OceanAdventureScriptBridge::ToJson(In);
			ForwardToScripts(Out);
		}));
	ForwardedChannels.Add(OceanAdventureBuildTags::Message_Build_Failed);
}

void UOceanAdventureScriptMessageBridge::UnregisterForwarders()
{
	// The router lives on the game instance and may already be gone while world subsystems
	// tear down; the handle holds a weak reference and turns into a no-op, whereas asking
	// UGameplayMessageSubsystem::Get for it here would assert on a null router.
	for (FGameplayMessageListenerHandle& Handle : ListenerHandles)
	{
		Handle.Unregister();
	}

	ListenerHandles.Reset();
	ForwardedChannels.Reset();
}
