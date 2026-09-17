// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/ScriptEventBus.h"

#include "ScriptCoreRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptEventBus)

void UScriptEventBus::Emit(FName Channel, const FString& PayloadJson)
{
	if (Channel.IsNone())
	{
		UE_LOG(LogScriptCore, Warning, TEXT("[Script] Emit ignored: no channel"));
		return;
	}

	// Broadcast copies its invocation list, so a handler that subscribes or unsubscribes while
	// it runs cannot invalidate the iteration underneath us.
	OnEvent.Broadcast(Channel, PayloadJson);
	NativeListeners.Broadcast(Channel, PayloadJson);
}

void UScriptEventBus::ClearScriptHandlers()
{
	// Clears Blueprint bindings too. Nothing but script subscribes here -- a Blueprint that
	// wants these events subscribes through OnAnyEvent(), which this deliberately leaves alone.
	const bool bHadSubscribers = OnEvent.IsBound();
	OnEvent.Clear();

	if (bHadSubscribers)
	{
		UE_LOG(LogScriptCore, Verbose, TEXT("[Script] Dropped script subscriptions on the event bus"));
	}
}
