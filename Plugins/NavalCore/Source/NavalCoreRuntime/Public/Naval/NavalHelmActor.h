// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "NavalHelmActor.generated.h"

class UBoxComponent;
class UNavalHelmComponent;
class UNavalPartComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * The deck console players call 主舵台, plus the reinforced seat under it.
 *
 * Two bodies on purpose (design 8.3.1). The wheel is tall, obvious and carries no collision:
 * it exists to be walked up to and to show state, so that hitting a visible wheel from across
 * the water cannot paralyse a whole ship. The wide, low core seat under the deck is the real
 * damage body, with 2.5-3x a normal wall's durability, which usually means an attacker has
 * to break through the hull's outer walls or board before they can touch it.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalHelmActor : public AActor
{
	GENERATED_BODY()

public:
	ANavalHelmActor();

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UNavalPartComponent* GetCorePart() const { return CorePart; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UStaticMeshComponent* GetConsoleMesh() const { return ConsoleMesh; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UBoxComponent* GetCoreSeatCollision() const { return CoreSeatCollision; }

	/** The helm component on the vessel this console belongs to. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UNavalHelmComponent* GetHelmComponent() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visual and interaction only -- deliberately not a damage body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UStaticMeshComponent> ConsoleMesh;

	/** 加固舵芯座: the wide, low body that shots actually have to reach. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UBoxComponent> CoreSeatCollision;

	/**
	 * Part component, not a default subobject by accident: the helm core is what the actor
	 * is, so it cannot be injected per experience the way optional abilities are.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UNavalPartComponent> CorePart;
};
