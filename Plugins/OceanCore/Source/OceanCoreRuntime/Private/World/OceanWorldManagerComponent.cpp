// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/OceanWorldManagerComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "OceanCoreRuntimeModule.h"
#include "World/OceanChunkActor.h"
#include "World/OceanChunkInvokerComponent.h"
#include "World/OceanGenerationSettings.h"

UOceanWorldManagerComponent::UOceanWorldManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	ChunkClass = AOceanChunkActor::StaticClass();
}

void UOceanWorldManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (GenerationSettings)
	{
		WorldSeed = GenerationSettings->GetWorldSeed();
		ChunkSize = GenerationSettings->GetChunkSize();
	}

	ChunkSize = GetSafeChunkSize();
	RefreshInterval = FMath::Max(0.05f, RefreshInterval);
	UnloadGraceSeconds = FMath::Max(0.0f, UnloadGraceSeconds);

	if (!ChunkClass)
	{
		LogMissingChunkClass();
	}

	GetWorld()->GetTimerManager().SetTimer(
		RefreshTimer, this, &ThisClass::RefreshChunks, RefreshInterval, true);
	RefreshChunks();
}

void UOceanWorldManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	if (HasAuthority())
	{
		for (const TPair<FIntPoint, TWeakObjectPtr<AOceanChunkActor>>& Pair : ActiveChunks)
		{
			if (AOceanChunkActor* Chunk = Pair.Value.Get())
			{
				Chunk->Destroy();
			}
		}
	}

	Invokers.Reset();
	ActiveChunks.Reset();
	PendingUnloadDeadlines.Reset();

	Super::EndPlay(EndPlayReason);
}

void UOceanWorldManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UOceanWorldManagerComponent, WorldSeed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UOceanWorldManagerComponent, ChunkSize, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UOceanWorldManagerComponent, ChunkBaseZ, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UOceanWorldManagerComponent, GenerationSettings, COND_InitialOnly);
}

UOceanWorldManagerComponent* UOceanWorldManagerComponent::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}

	if (const UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->FindComponentByClass<UOceanWorldManagerComponent>();
		}
	}

	return nullptr;
}

void UOceanWorldManagerComponent::RegisterInvoker(UOceanChunkInvokerComponent* Invoker)
{
	if (!HasAuthority() || !IsValid(Invoker) || Invoker->GetWorld() != GetWorld())
	{
		return;
	}

	if (!Invokers.ContainsByPredicate([Invoker](const TWeakObjectPtr<UOceanChunkInvokerComponent>& Entry)
		{ return Entry.Get() == Invoker; }))
	{
		Invokers.Add(Invoker);
		UE_LOG(LogOceanCore, Verbose, TEXT("Registered chunk invoker %s with %s."),
			*GetNameSafe(Invoker->GetOwner()), *GetNameSafe(GetOwner()));
	}

	RequestRefreshChunks();
}

void UOceanWorldManagerComponent::UnregisterInvoker(UOceanChunkInvokerComponent* Invoker)
{
	if (!HasAuthority())
	{
		return;
	}

	Invokers.RemoveAll([Invoker](const TWeakObjectPtr<UOceanChunkInvokerComponent>& Entry)
		{ return !Entry.IsValid() || Entry.Get() == Invoker; });
	RequestRefreshChunks();
}

void UOceanWorldManagerComponent::RequestRefreshChunks()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	bRefreshRequested = true;
	if (!bRefreshInProgress && !bRefreshQueued)
	{
		RefreshChunks();
	}
}

FIntPoint UOceanWorldManagerComponent::GetChunkCoordFromWorldLocation(FVector WorldLocation) const
{
	const float SafeChunkSize = GetSafeChunkSize();
	return FIntPoint(
		FMath::FloorToInt(WorldLocation.X / SafeChunkSize),
		FMath::FloorToInt(WorldLocation.Y / SafeChunkSize));
}

FVector UOceanWorldManagerComponent::GetChunkWorldOrigin(FIntPoint ChunkCoord) const
{
	const float SafeChunkSize = GetSafeChunkSize();
	return FVector(
		static_cast<double>(ChunkCoord.X) * SafeChunkSize,
		static_cast<double>(ChunkCoord.Y) * SafeChunkSize,
		ChunkBaseZ);
}

AOceanChunkActor* UOceanWorldManagerComponent::GetChunkAtCoord(FIntPoint ChunkCoord) const
{
	if (const TWeakObjectPtr<AOceanChunkActor>* FoundChunk = ActiveChunks.Find(ChunkCoord))
	{
		return FoundChunk->Get();
	}

	return nullptr;
}

int32 UOceanWorldManagerComponent::GetActiveChunkCount() const
{
	return ActiveChunks.Num();
}

TArray<FIntPoint> UOceanWorldManagerComponent::GetActiveChunkCoords() const
{
	TArray<FIntPoint> Result;
	ActiveChunks.GetKeys(Result);
	Result.Sort([](const FIntPoint& A, const FIntPoint& B)
		{ return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	return Result;
}

int32 UOceanWorldManagerComponent::GetRegisteredInvokerCount() const
{
	int32 ValidInvokerCount = 0;
	for (const TWeakObjectPtr<UOceanChunkInvokerComponent>& Entry : Invokers)
	{
		ValidInvokerCount += Entry.IsValid() ? 1 : 0;
	}
	return ValidInvokerCount;
}

FOceanWaterSurfaceSample UOceanWorldManagerComponent::SampleWaterSurface(
	FVector WorldLocation, float TimeSeconds) const
{
	const FIntPoint ChunkCoord = GetChunkCoordFromWorldLocation(WorldLocation);
	if (const AOceanChunkActor* Chunk = GetChunkAtCoord(ChunkCoord))
	{
		return Chunk->SampleWaterSurface(WorldLocation, TimeSeconds);
	}

	const UOceanGenerationSettings* Settings = GenerationSettings
		? GenerationSettings.Get()
		: GetDefault<UOceanGenerationSettings>();
	FOceanWaterSurfaceSample Result = Settings->SampleWaterSurface(
		FVector2D(WorldLocation.X, WorldLocation.Y), TimeSeconds);
	Result.Position.Z += ChunkBaseZ;
	return Result;
}

bool UOceanWorldManagerComponent::HasAuthority() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}

void UOceanWorldManagerComponent::RefreshChunks()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	bRefreshQueued = false;

	if (bRefreshInProgress)
	{
		bRefreshRequested = true;
		if (!bRefreshQueued)
		{
			bRefreshQueued = true;
			GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshChunks);
		}
		return;
	}

	bRefreshRequested = false;
	if (!ChunkClass)
	{
		// Keep the periodic timer alive so runtime reconfiguration can recover automatically.
		LogMissingChunkClass();
		return;
	}

	TGuardValue<bool> RefreshGuard(bRefreshInProgress, true);
	CleanupInvalidReferences();

	const TSet<FIntPoint> RequiredChunks = BuildRequiredChunkSet();
	for (const FIntPoint& ChunkCoord : RequiredChunks)
	{
		PendingUnloadDeadlines.Remove(ChunkCoord);
		if (!ActiveChunks.Contains(ChunkCoord))
		{
			SpawnChunk(ChunkCoord);
		}
	}

	const double Now = GetWorld()->GetTimeSeconds();
	TArray<FIntPoint> ChunksToDestroy;
	for (const TPair<FIntPoint, TWeakObjectPtr<AOceanChunkActor>>& Pair : ActiveChunks)
	{
		if (RequiredChunks.Contains(Pair.Key))
		{
			continue;
		}

		double* Deadline = PendingUnloadDeadlines.Find(Pair.Key);
		if (!Deadline)
		{
			if (UnloadGraceSeconds <= 0.0f)
			{
				ChunksToDestroy.Add(Pair.Key);
			}
			else
			{
				PendingUnloadDeadlines.Add(Pair.Key, Now + UnloadGraceSeconds);
			}
		}
		else if (Now >= *Deadline)
		{
			ChunksToDestroy.Add(Pair.Key);
		}
	}

	for (const FIntPoint& ChunkCoord : ChunksToDestroy)
	{
		DestroyChunk(ChunkCoord);
	}

	// Requests raised while spawning or destroying are coalesced into one follow-up pass.
	if (bRefreshRequested && !bRefreshQueued)
	{
		bRefreshQueued = true;
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshChunks);
	}
}

void UOceanWorldManagerComponent::CleanupInvalidReferences()
{
	Invokers.RemoveAll([](const TWeakObjectPtr<UOceanChunkInvokerComponent>& Entry)
		{ return !Entry.IsValid(); });

	TArray<FIntPoint> InvalidChunkCoords;
	for (const TPair<FIntPoint, TWeakObjectPtr<AOceanChunkActor>>& Pair : ActiveChunks)
	{
		if (!Pair.Value.IsValid())
		{
			InvalidChunkCoords.Add(Pair.Key);
		}
	}

	for (const FIntPoint& ChunkCoord : InvalidChunkCoords)
	{
		ActiveChunks.Remove(ChunkCoord);
		PendingUnloadDeadlines.Remove(ChunkCoord);
	}
}

TSet<FIntPoint> UOceanWorldManagerComponent::BuildRequiredChunkSet() const
{
	TSet<FIntPoint> RequiredChunks;
	for (const TWeakObjectPtr<UOceanChunkInvokerComponent>& InvokerEntry : Invokers)
	{
		const UOceanChunkInvokerComponent* Invoker = InvokerEntry.Get();
		// Activation is an authority-side generation switch. Relevancy remains permissive so
		// chunks generated for another active invoker may still replicate to this viewer.
		if (!Invoker || !Invoker->IsActive())
		{
			continue;
		}

		const FIntPoint CenterCoord = GetChunkCoordFromWorldLocation(Invoker->GetInvokerLocation());
		const int32 Radius = FMath::Max(0, Invoker->GetActiveRadius());
		for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
		{
			for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
			{
				RequiredChunks.Add(CenterCoord + FIntPoint(OffsetX, OffsetY));
			}
		}
	}
	return RequiredChunks;
}

AOceanChunkActor* UOceanWorldManagerComponent::SpawnChunk(FIntPoint ChunkCoord)
{
	UWorld* World = GetWorld();
	if (!World || !ChunkClass)
	{
		LogMissingChunkClass();
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = nullptr;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const float SafeChunkSize = GetSafeChunkSize();
	AOceanChunkActor* Chunk = World->SpawnActor<AOceanChunkActor>(
		ChunkClass, GetChunkWorldOrigin(ChunkCoord), FRotator::ZeroRotator, SpawnParameters);
	if (!Chunk || !Chunk->InitializeChunk(
		ChunkCoord, SafeChunkSize, WorldSeed, ChunkBaseZ, GenerationSettings))
	{
		UE_LOG(LogOceanCore, Error, TEXT("Failed to initialize ocean chunk [%d, %d]."),
			ChunkCoord.X, ChunkCoord.Y);
		if (Chunk)
		{
			Chunk->Destroy();
		}
		return nullptr;
	}

	ActiveChunks.Add(ChunkCoord, Chunk);
	OnChunkActivated.Broadcast(Chunk);
	return Chunk;
}

void UOceanWorldManagerComponent::DestroyChunk(FIntPoint ChunkCoord)
{
	TWeakObjectPtr<AOceanChunkActor> ChunkEntry;
	if (!ActiveChunks.RemoveAndCopyValue(ChunkCoord, ChunkEntry))
	{
		return;
	}

	PendingUnloadDeadlines.Remove(ChunkCoord);
	if (AOceanChunkActor* Chunk = ChunkEntry.Get())
	{
		OnChunkDeactivated.Broadcast(Chunk);
		Chunk->Destroy();
	}
}

void UOceanWorldManagerComponent::LogMissingChunkClass()
{
	if (bLoggedMissingChunkClass)
	{
		return;
	}

	bLoggedMissingChunkClass = true;
	UE_LOG(LogOceanCore, Error, TEXT("%s: ChunkClass is not configured. Ocean chunks will not spawn."),
		*GetNameSafe(GetOwner()));
}
