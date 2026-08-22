// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalLoadComponent.h"

#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildStructureComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalPieceFragments.h"
#include "Naval/NavalVesselComponent.h"
#include "NavalCoreRuntimeModule.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalLoadComponent)

UNavalLoadComponent::UNavalLoadComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void UNavalLoadComponent::BeginPlay()
{
	Super::BeginPlay();

	BindHostDelegates();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RecomputeLoad();
	}
	else
	{
		BroadcastLoadState();
	}
}

void UNavalLoadComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBuildStructureComponent* StructureComponent = Structure.Get())
	{
		StructureComponent->OnStructureChanged.Remove(StructureChangedHandle);
	}
	if (UNavalVesselComponent* VesselComponent = Vessel.Get())
	{
		VesselComponent->OnVesselPartsChanged.Remove(PartsChangedHandle);
	}
	StructureChangedHandle.Reset();
	PartsChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void UNavalLoadComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalLoadComponent, LoadState);
}

void UNavalLoadComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* OwnerActor = GetOwner();
	UNavalVesselComponent* VesselComponent = Vessel.Get();
	if (!LoadState.bDangerousDraft || !OwnerActor || !OwnerActor->HasAuthority() || !VesselComponent)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// 持续进水. The crew keeps full control while this runs -- they can repair the pontoon,
	// tear off a cannon or simply win the fight before the hull runs out. A throttled tick
	// is handed the accumulated DeltaTime, so the rate stays the same at any tick interval.
	const float DamagePerSecond =
		VesselComponent->GetMaxHullDurability() * DangerousDraftHullFractionPerSecond;
	VesselComponent->ApplyHullDamage(DamagePerSecond * DeltaTime, nullptr);
}

void UNavalLoadComponent::BindHostDelegates()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	Structure = OwnerActor->FindComponentByClass<UBuildStructureComponent>();
	Vessel = OwnerActor->FindComponentByClass<UNavalVesselComponent>();

	if (UBuildStructureComponent* StructureComponent = Structure.Get())
	{
		StructureChangedHandle =
			StructureComponent->OnStructureChanged.AddUObject(this, &UNavalLoadComponent::HandleStructureChanged);
	}
	if (UNavalVesselComponent* VesselComponent = Vessel.Get())
	{
		PartsChangedHandle =
			VesselComponent->OnVesselPartsChanged.AddUObject(this, &UNavalLoadComponent::HandlePartsChanged);
	}
}

void UNavalLoadComponent::HandleStructureChanged()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RecomputeLoad();
	}
}

void UNavalLoadComponent::HandlePartsChanged(UNavalVesselComponent* /*ChangedVessel*/)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RecomputeLoad();
	}
}

void UNavalLoadComponent::AccumulateStructureContributions(
	float& OutTonnage, float& OutBuoyancy, float& OutThrust) const
{
	const UBuildStructureComponent* StructureComponent = Structure.Get();
	const UBuildPieceCatalog* Catalog = StructureComponent ? StructureComponent->GetCatalog() : nullptr;
	if (!StructureComponent || !Catalog)
	{
		return;
	}

	for (const FBuildPieceEntry& Entry : StructureComponent->GetEntries())
	{
		const UBuildPieceDefinition* Definition = Catalog->GetByIndex(Entry.PieceIndex);
		if (!Definition)
		{
			continue;
		}

		// A piece that spawns its own Actor reports through that Actor's part component
		// instead, so counting it here as well would double its weight.
		if (Definition->FindFragment<UBuildPieceFragment_SpawnActor>() != nullptr)
		{
			continue;
		}

		if (const UBuildPieceFragment_NavalLoad* Load = Definition->FindFragment<UBuildPieceFragment_NavalLoad>())
		{
			OutTonnage += Load->Tonnage;
			OutBuoyancy += Load->BuoyancyCapacity;
			OutThrust += Load->Thrust;
		}
	}
}

void UNavalLoadComponent::RecomputeLoad()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	float Tonnage = BaseTonnage;
	float Buoyancy = BaseBuoyancyCapacity;
	float Thrust = BaseThrust;

	AccumulateStructureContributions(Tonnage, Buoyancy, Thrust);

	if (const UNavalVesselComponent* VesselComponent = Vessel.Get())
	{
		for (const TWeakObjectPtr<UNavalPartComponent>& Entry : VesselComponent->GetParts())
		{
			const UNavalPartComponent* Part = Entry.Get();
			if (!Part)
			{
				continue;
			}

			// Weight is unconditional: a shot-out pontoon is still bolted to the raft. Only
			// its capacity goes away, which is exactly what creates the dangerous-draft state.
			Tonnage += Part->GetTonnageContribution();
			Buoyancy += Part->GetBuoyancyContribution();
			Thrust += Part->GetThrustContribution();
		}
	}

	FNavalLoadState NewState;
	NewState.Tonnage = Tonnage;
	NewState.BuoyancyCapacity = FMath::Max(1.0f, Buoyancy);
	NewState.Thrust = Thrust;
	NewState.LoadRatio = NewState.Tonnage / NewState.BuoyancyCapacity;
	NewState.ThrustRatio = NewState.Thrust / FMath::Max(1.0f, NewState.Tonnage);
	NewState.LoadBand = NavalLoad::BandForLoadRatio(NewState.LoadRatio);
	NewState.ThrustBand = NavalLoad::BandForThrustRatio(NewState.ThrustRatio);
	NewState.bDangerousDraft = NewState.LoadBand == ENavalLoadBand::SeverelyOverloaded;

	ApplyLoadState(NewState);
}

void UNavalLoadComponent::ApplyLoadState(const FNavalLoadState& NewState)
{
	const bool bEnteredDangerousDraft = NewState.bDangerousDraft && !LoadState.bDangerousDraft;
	if (LoadState == NewState)
	{
		return;
	}

	LoadState = NewState;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	SetComponentTickEnabled(LoadState.bDangerousDraft);
	OnLoadChanged.Broadcast(LoadState);
	BroadcastLoadState();

	if (bEnteredDangerousDraft)
	{
		FNavalAlertMessage Alert;
		Alert.Vessel = GetOwner();
		Alert.AlertTag = NavalGameplayTags::Alert_DangerousDraft;
		Alert.WorldLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		Alert.TeamId = Vessel.IsValid() ? Vessel->GetTeamId() : INDEX_NONE;
		if (const UWorld* World = GetWorld())
		{
			UGameplayMessageSubsystem::Get(World).BroadcastMessage(
				NavalGameplayTags::Message_Vessel_Alert, Alert);
		}

		UE_LOG(
			LogNavalCore,
			Display,
			TEXT("[Load] Dangerous draft vessel=%s W=%.1f C=%.1f ratio=%.2f"),
			*GetNameSafe(GetOwner()),
			LoadState.Tonnage,
			LoadState.BuoyancyCapacity,
			LoadState.LoadRatio);
	}
}

void UNavalLoadComponent::OnRep_LoadState()
{
	OnLoadChanged.Broadcast(LoadState);
	BroadcastLoadState();
}

void UNavalLoadComponent::BroadcastLoadState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FNavalLoadStateMessage Message;
	Message.Vessel = GetOwner();
	Message.LoadState = LoadState;
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(NavalGameplayTags::Message_Vessel_Load, Message);
}

FNavalHandlingScalars UNavalLoadComponent::GetHandlingScalars() const
{
	FNavalHandlingScalars Scalars = NavalLoad::ScalarsForLoadBand(LoadState.LoadBand);
	Scalars.CombineWith(NavalLoad::ScalarsForThrustBand(LoadState.ThrustBand));
	return Scalars;
}

FNavalLoadState UNavalLoadComponent::PredictLoadState(
	float AdditionalTonnage, float AdditionalBuoyancy, float AdditionalThrust) const
{
	FNavalLoadState Predicted = LoadState;
	Predicted.Tonnage = FMath::Max(0.0f, LoadState.Tonnage + AdditionalTonnage);
	Predicted.BuoyancyCapacity = FMath::Max(1.0f, LoadState.BuoyancyCapacity + AdditionalBuoyancy);
	Predicted.Thrust = FMath::Max(0.0f, LoadState.Thrust + AdditionalThrust);
	Predicted.LoadRatio = Predicted.Tonnage / Predicted.BuoyancyCapacity;
	Predicted.ThrustRatio = Predicted.Thrust / FMath::Max(1.0f, Predicted.Tonnage);
	Predicted.LoadBand = NavalLoad::BandForLoadRatio(Predicted.LoadRatio);
	Predicted.ThrustBand = NavalLoad::BandForThrustRatio(Predicted.ThrustRatio);
	Predicted.bDangerousDraft = Predicted.LoadBand == ENavalLoadBand::SeverelyOverloaded;
	return Predicted;
}

bool UNavalLoadComponent::CanAcceptPiece(
	float AdditionalTonnage, float AdditionalBuoyancy, FGameplayTag& OutFailReason) const
{
	// Anything that adds capacity or removes weight is always allowed -- that is the way out
	// of severe overload, so it must never be gated by severe overload.
	if (AdditionalTonnage <= 0.0f || AdditionalBuoyancy > 0.0f)
	{
		return true;
	}

	const FNavalLoadState Predicted = PredictLoadState(AdditionalTonnage, AdditionalBuoyancy, 0.0f);
	if (Predicted.LoadBand == ENavalLoadBand::SeverelyOverloaded)
	{
		OutFailReason = NavalGameplayTags::Fail_SevereOverload;
		return false;
	}

	return true;
}
