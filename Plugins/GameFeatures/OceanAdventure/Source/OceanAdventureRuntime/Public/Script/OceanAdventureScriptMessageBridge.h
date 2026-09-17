// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "Script/OceanAdventureScriptTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "OceanAdventureScriptMessageBridge.generated.h"

/** One forwarded gameplay message, flattened into the shape scripts take. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOceanAdventureScriptMessageSignature, const FOceanAdventureScriptMessage&, Message);

/**
 * Puts the game's existing gameplay messages in reach of scripts.
 *
 * Every reaction in this game already travels as a gameplay message -- a shot landed, a hull
 * changed state, a lift was refused -- precisely so that HUD, audio and telemetry can each
 * subscribe without being wired into the system that raised it. A script is one more such
 * subscriber, so this bridge listens once per channel in C++ and re-publishes them all on one
 * delegate, which scripts filter by Message.Channel.
 *
 * One delegate rather than one per channel, for the same reason the hooks use properties: a
 * per-channel subscription would mean handing a delegate to a UFUNCTION as an argument, which
 * is the part of the script boundary whose marshalling differs most between VM releases.
 * GetForwardedChannels() exists so a script can still fail loudly on a channel nobody forwards
 * instead of waiting forever for an event that will not come.
 *
 * Two buses exist and they are not interchangeable:
 *
 *  - This one is typed and gameplay-shaped. It carries live AActor pointers, because a
 *    reaction that cannot name the actor it is reacting to cannot do anything about it.
 *  - UScriptEventBus, in ScriptCore, carries JSON on free-form channels and knows nothing
 *    about gameplay. Use it for things with no actor: a match phase, a tuning reload, a
 *    counter one script raises for another.
 *
 * Adding a channel here is a handful of lines and one rebuild. That cost is the reason the
 * flattening is explicit rather than reflective: a generic walker over any USTRUCT would save
 * those lines and, in exchange, would silently change what a script sees whenever anybody
 * renamed a field in a message it had never heard of.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureScriptMessageBridge : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script", meta = (WorldContext = "WorldContextObject"))
	static UOceanAdventureScriptMessageBridge* Get(const UObject* WorldContextObject);

	/** Scripts subscribe here: OnGameplayMessage.Add(fn). Fires for every forwarded channel. */
	UPROPERTY(BlueprintAssignable, Category = "OceanAdventure|Script")
	FOceanAdventureScriptMessageSignature OnGameplayMessage;

	/** Which channels this build forwards, so a script can fail loudly on a typo. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script")
	TArray<FGameplayTag> GetForwardedChannels() const { return ForwardedChannels; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void ForwardToScripts(const FOceanAdventureScriptMessage& Message);
	void ClearScriptHandlers();

	void RegisterForwarders();
	void UnregisterForwarders();

	TArray<FGameplayMessageListenerHandle> ListenerHandles;
	TArray<FGameplayTag> ForwardedChannels;
	FDelegateHandle ScriptStoppingHandle;
};
