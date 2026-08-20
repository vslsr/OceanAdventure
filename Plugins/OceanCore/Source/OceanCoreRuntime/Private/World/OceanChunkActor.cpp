// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/OceanChunkActor.h"

#include "Components/GameFrameworkComponentManager.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "OceanCoreTypes.h"
#include "World/OceanGenerationSettings.h"
#include "World/OceanChunkInvokerComponent.h"

namespace
{
	float CalculateChunkCullDistanceSquared(float ChunkSize)
	{
		const float MaxRelevantDistance = FMath::Max(0.0f, ChunkSize)
			* (static_cast<float>(OceanCore::MaxSupportedChunkRadius) + OceanCore::ChunkCullDistanceMargin)
			* FMath::Sqrt(2.0f);
		return FMath::Square(MaxRelevantDistance);
	}
}

AOceanChunkActor::AOceanChunkActor()
{
	#if ENABLE_DRAW_DEBUG
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.25f;
	#else
	PrimaryActorTick.bCanEverTick = false;
	#endif

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	bReplicates = true;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(2.0f);
	SetMinNetUpdateFrequency(1.0f);
	SetNetCullDistanceSquared(CalculateChunkCullDistanceSquared(20000.0f));
}

void AOceanChunkActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

void AOceanChunkActor::BeginPlay()
{
	Super::BeginPlay();
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
		this, UGameFrameworkComponentManager::NAME_GameActorReady);

	bBeginPlayFinished = true;
	#if ENABLE_DRAW_DEBUG
	SetActorTickEnabled(bDrawDebugBounds);
	#endif
	NotifyChunkInitialized();
}

void AOceanChunkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(this);
	Super::EndPlay(EndPlayReason);
}

void AOceanChunkActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	#if ENABLE_DRAW_DEBUG
	if (!bDrawDebugBounds || !ChunkState.bInitialized || !GetWorld())
	{
		return;
	}

	const float HalfSize = ChunkState.ChunkSize * 0.5f;
	const FVector Center = GetChunkWorldOrigin() + FVector(HalfSize, HalfSize, 50.0f);
	const float Lifetime = PrimaryActorTick.TickInterval * 1.1f;
	const FColor Color = DebugColor.ToFColor(true);

	DrawDebugBox(GetWorld(), Center, FVector(HalfSize, HalfSize, 50.0f), FQuat::Identity,
		Color, false, Lifetime, 0, 10.0f);
	DrawDebugString(GetWorld(), Center + FVector(0.0f, 0.0f, 100.0f),
		FString::Printf(TEXT("Chunk [%d, %d]"), ChunkState.ChunkCoord.X, ChunkState.ChunkCoord.Y),
		nullptr, Color, Lifetime, false, 1.0f);
	#endif
}

bool AOceanChunkActor::IsNetRelevantFor(
	const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (!ChunkState.bInitialized || ChunkState.ChunkSize <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const UOceanChunkInvokerComponent* ViewerInvoker = ViewTarget
		? ViewTarget->FindComponentByClass<UOceanChunkInvokerComponent>()
		: nullptr;

	if (!ViewerInvoker && RealViewer)
	{
		ViewerInvoker = RealViewer->FindComponentByClass<UOceanChunkInvokerComponent>();
	}

	if (!ViewerInvoker)
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(RealViewer))
		{
			if (const APawn* Pawn = PlayerController->GetPawn())
			{
				ViewerInvoker = Pawn->FindComponentByClass<UOceanChunkInvokerComponent>();
			}
		}
	}

	if (!ViewerInvoker)
	{
		return false;
	}

	const FVector InvokerLocation = ViewerInvoker->GetInvokerLocation();
	const FIntPoint InvokerCoord(
		FMath::FloorToInt(InvokerLocation.X / ChunkState.ChunkSize),
		FMath::FloorToInt(InvokerLocation.Y / ChunkState.ChunkSize));
	const int32 Radius = FMath::Max(0, ViewerInvoker->GetActiveRadius());

	return FMath::Abs(ChunkState.ChunkCoord.X - InvokerCoord.X) <= Radius
		&& FMath::Abs(ChunkState.ChunkCoord.Y - InvokerCoord.Y) <= Radius;
}

void AOceanChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AOceanChunkActor, ChunkState, COND_InitialOnly);
}

bool AOceanChunkActor::InitializeChunk(
	FIntPoint InChunkCoord,
	float InChunkSize,
	int32 InWorldSeed,
	float InBaseZ,
	UOceanGenerationSettings* InGenerationSettings)
{
	if (!HasAuthority() || InChunkSize <= UE_SMALL_NUMBER)
	{
		return false;
	}

	if (ChunkState.bInitialized)
	{
		return ChunkState.ChunkCoord == InChunkCoord
			&& FMath::IsNearlyEqual(ChunkState.ChunkSize, InChunkSize)
			&& ChunkState.WorldSeed == InWorldSeed
			&& FMath::IsNearlyEqual(ChunkState.BaseZ, InBaseZ)
			&& ChunkState.GenerationSettings == InGenerationSettings;
	}

	ChunkState.ChunkCoord = InChunkCoord;
	ChunkState.ChunkSize = InChunkSize;
	ChunkState.WorldSeed = InWorldSeed;
	ChunkState.BaseZ = InBaseZ;
	ChunkState.GenerationSettings = InGenerationSettings;
	ChunkState.bInitialized = true;

	// Keep coarse network culling aligned with the same radius limit enforced by invokers.
	SetNetCullDistanceSquared(CalculateChunkCullDistanceSquared(InChunkSize));

	SetActorLocation(GetChunkWorldOrigin());
	ForceNetUpdate();
	NotifyChunkInitialized();
	return true;
}

int32 AOceanChunkActor::GetChunkSeed() const
{
	uint32 Hash = 2166136261u;
	Hash = (Hash ^ static_cast<uint32>(ChunkState.WorldSeed)) * 16777619u;
	Hash = (Hash ^ static_cast<uint32>(ChunkState.ChunkCoord.X)) * 16777619u;
	Hash = (Hash ^ static_cast<uint32>(ChunkState.ChunkCoord.Y)) * 16777619u;
	return static_cast<int32>(Hash & 0x7fffffffu);
}

FVector AOceanChunkActor::GetChunkWorldOrigin() const
{
	return FVector(
		static_cast<double>(ChunkState.ChunkCoord.X) * ChunkState.ChunkSize,
		static_cast<double>(ChunkState.ChunkCoord.Y) * ChunkState.ChunkSize,
		ChunkState.BaseZ);
}

void AOceanChunkActor::OnRep_ChunkState()
{
	if (ChunkState.bInitialized)
	{
		// The replicated state is authoritative for position; spawn-bunch location is only provisional.
		SetActorLocation(GetChunkWorldOrigin());
	}
	NotifyChunkInitialized();
}

void AOceanChunkActor::NotifyChunkInitialized()
{
	if (!bBeginPlayFinished || !ChunkState.bInitialized || bInitializationNotified)
	{
		return;
	}

	bInitializationNotified = true;
	OnChunkInitialized.Broadcast(this);
	BP_OnChunkInitialized();
}
