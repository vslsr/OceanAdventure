// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Naval/NavalCoreTypes.h"

#include "NavalLoadComponent.generated.h"

class UBuildStructureComponent;
class UNavalPartComponent;
class UNavalVesselComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavalLoadChanged, const FNavalLoadState& /*LoadState*/);

/**
 * Tonnage, buoyancy capacity and thrust for one vessel -- the whole of design 7.11.
 *
 * Two sources feed it and both matter. Instanced pieces (decks, walls, windows) never become
 * Actors, so their weight is read straight off the piece definition's naval fragment; parts
 * that do spawn Actors (pontoons, thrusters, rudders, guns) report through their part
 * component, where losing one stops its contribution without stopping its weight. That
 * asymmetry is the point: shooting a pontoon off is what pushes a raft into dangerous draft.
 *
 * Recomputed on structure and part changes only. Nothing here runs per frame except the
 * dangerous-draft water ingress.
 */
UCLASS(BlueprintType, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalLoadComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalLoadComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Naval|Load")
	const FNavalLoadState& GetLoadState() const { return LoadState; }

	/** Combined load-band and thrust-band handling multipliers movement applies. */
	UFUNCTION(BlueprintPure, Category = "Naval|Load")
	FNavalHandlingScalars GetHandlingScalars() const;

	/**
	 * Build gate. Severe overload allows only removal, pontoons and repairs, so a player can
	 * always dig themselves out but can never keep stacking guns onto a sinking raft.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|Load")
	bool CanAcceptPiece(float AdditionalTonnage, float AdditionalBuoyancy, FGameplayTag& OutFailReason) const;

	/** Preview helper: what the bands would read after placing this piece. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Load")
	FNavalLoadState PredictLoadState(float AdditionalTonnage, float AdditionalBuoyancy, float AdditionalThrust) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Load")
	void RecomputeLoad();

	FOnNavalLoadChanged OnLoadChanged;

protected:
	/** 初始核心筏体（含基础甲板）: design 7.11 starting scale is W+24 / C+50 / P+40. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float BaseTonnage = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "1.0"))
	float BaseBuoyancyCapacity = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float BaseThrust = 40.0f;

	/**
	 * Hull lost per second of dangerous draft. Design 7.11 wants 10-15 seconds in which the
	 * crew can repair, dump weight or fight back -- not an instant loss and not a free ride.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DangerousDraftHullFractionPerSecond = 0.06f;

	UPROPERTY(ReplicatedUsing = OnRep_LoadState)
	FNavalLoadState LoadState;

	UFUNCTION()
	void OnRep_LoadState();

private:
	void BindHostDelegates();
	void HandleStructureChanged();
	void HandlePartsChanged(UNavalVesselComponent* Vessel);
	void ApplyLoadState(const FNavalLoadState& NewState);
	void BroadcastLoadState() const;

	/** Sums the naval fragments of every instanced piece currently on the structure. */
	void AccumulateStructureContributions(float& OutTonnage, float& OutBuoyancy, float& OutThrust) const;

	TWeakObjectPtr<UBuildStructureComponent> Structure;
	TWeakObjectPtr<UNavalVesselComponent> Vessel;
	FDelegateHandle StructureChangedHandle;
	FDelegateHandle PartsChangedHandle;
};
