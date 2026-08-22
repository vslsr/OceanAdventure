// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"

#include "OceanAdventureNavalDamageRelay.generated.h"

/**
 * Applies the character half of naval projectile damage.
 *
 * NavalCore stops at structure and hull because those are framework truth that cheats and
 * save restore have to reach without GAS. Characters live in Lyra's attribute set, so the
 * framework publishes the pending damage on a message channel and this subsystem -- the only
 * place that knows both sides exist -- turns it into a GameplayEffect.
 *
 * Server only: the message is broadcast where the impact was resolved.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalDamageRelay : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void OnProjectileImpact(FGameplayTag Channel, const struct FNavalProjectileImpactMessage& Message);

	FGameplayMessageListenerHandle ImpactListenerHandle;
};
