// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "ScriptEventBus.generated.h"

/**
 * One event. Scripts subscribe to the bus, not to a channel, and filter in the handler.
 *
 * That shape is not a simplification for its own sake: binding per channel would mean handing
 * a delegate to a UFUNCTION as an argument, and how a script VM marshals *that* is the part of
 * the bridge that varies most between releases. Subscribing to a delegate *property* is the
 * one form every binding generator agrees on, so the boundary uses only that form.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FScriptEventSignature, FName, Channel, const FString&, PayloadJson);

/** Native-side listeners, which survive a reload because they are not script closures. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnScriptEvent, FName /*Channel*/, const FString& /*PayloadJson*/);

/**
 * The loose half of the C++ <-> TypeScript boundary: named channels carrying JSON.
 *
 * Two mechanisms exist on purpose, and picking the wrong one is the mistake this comment is
 * here to prevent:
 *
 *  - This bus carries the cheap, wide traffic that has no UObject in it -- a match phase, a
 *    tuning reload, a counter one script raises for another. JSON costs a serialize per event
 *    and cannot name an actor; in exchange a channel can gain a field with no C++ at all.
 *  - Anything per-hit, and anything that has to hand a script a real AActor, gets a typed
 *    delegate in the gameplay layer instead (UOceanAdventureScriptHooks for decisions,
 *    UOceanAdventureScriptMessageBridge for gameplay messages). Those cost a rebuild when
 *    their signature changes, which is the right trade at that frequency.
 *
 * Script subscriptions are dropped whenever the VM reloads. That is not tidiness: a hot reload
 * builds new closures, and without the sweep every save would leave the previous generation
 * subscribed, so one event would fire N times after N reloads -- and a rule that runs twice
 * looks like a balance bug, not like a scripting bug.
 */
UCLASS(BlueprintType)
class SCRIPTCORERUNTIME_API UScriptEventBus : public UObject
{
	GENERATED_BODY()

public:
	/** Scripts subscribe here. Fires for every channel; the handler decides what it cares about. */
	UPROPERTY(BlueprintAssignable, Category = "Script")
	FScriptEventSignature OnEvent;

	/** Publishes to a channel. Either side may call this. */
	UFUNCTION(BlueprintCallable, Category = "Script")
	void Emit(FName Channel, const FString& PayloadJson);

	UFUNCTION(BlueprintPure, Category = "Script")
	bool HasSubscribers() const { return OnEvent.IsBound(); }

	/** Native listeners are not script closures, so a reload leaves them alone. */
	FOnScriptEvent& OnAnyEvent() { return NativeListeners; }

	/** Called by the host immediately before the VM is torn down. */
	void ClearScriptHandlers();

private:
	FOnScriptEvent NativeListeners;
};
