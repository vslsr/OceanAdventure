// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/OceanChunkInvokerComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "OceanCoreRuntimeModule.h"
#include "OceanCoreTypes.h"
#include "World/OceanWorldManagerComponent.h"

UOceanChunkInvokerComponent::UOceanChunkInvokerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UOceanChunkInvokerComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	ApplyActiveRadius(ActiveRadius);
	TickRegistration();
	StartRegistrationTimer();
}

void UOceanChunkInvokerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRegistrationTimer();

	if (IsValid(RegisteredManager))
	{
		RegisteredManager->UnregisterInvoker(this);
	}
	RegisteredManager = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UOceanChunkInvokerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOceanChunkInvokerComponent, ActiveRadius);
}

void UOceanChunkInvokerComponent::SetActiveRadius(int32 InActiveRadius)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	const int32 BoundedRadius = ClampActiveRadius(InActiveRadius);
	if (OwnerActor->HasAuthority())
	{
		ApplyActiveRadius(BoundedRadius);
	}
	else if (ActiveRadius == BoundedRadius)
	{
		PendingActiveRadius = INDEX_NONE;
	}
	else
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const double Cooldown = FMath::Max(0.0, static_cast<double>(RadiusChangeCooldown));
		if (PendingActiveRadius == BoundedRadius && Now - LastRadiusRequestTime < Cooldown)
		{
			return;
		}

		PendingActiveRadius = BoundedRadius;
		LastRadiusRequestTime = Now;
		ServerSetActiveRadius(BoundedRadius);
	}
}

float UOceanChunkInvokerComponent::GetActiveRangeCentimeters(float ChunkSize) const
{
	return FMath::Max(0.0f, ChunkSize) * FMath::Max(0, ActiveRadius);
}

FVector UOceanChunkInvokerComponent::GetInvokerLocation() const
{
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

bool UOceanChunkInvokerComponent::IsRegisteredWithManager() const
{
	return IsValid(RegisteredManager);
}

void UOceanChunkInvokerComponent::OnRep_ActiveRadius()
{
	if (PendingActiveRadius == ActiveRadius)
	{
		PendingActiveRadius = INDEX_NONE;
	}
}

void UOceanChunkInvokerComponent::ServerSetActiveRadius_Implementation(int32 InActiveRadius)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double Cooldown = FMath::Max(0.0, static_cast<double>(RadiusChangeCooldown));
	if (Now - LastRadiusChangeTime < Cooldown)
	{
		return;
	}

	LastRadiusChangeTime = Now;
	ApplyActiveRadius(InActiveRadius);
}

bool UOceanChunkInvokerComponent::ServerSetActiveRadius_Validate(int32 InActiveRadius)
{
	return InActiveRadius >= 0 && InActiveRadius <= OceanCore::MaxSupportedChunkRadius;
}

int32 UOceanChunkInvokerComponent::ClampActiveRadius(int32 InActiveRadius) const
{
	const int32 SanitizedMin = FMath::Clamp(MinActiveRadius, 0, OceanCore::MaxSupportedChunkRadius);
	const int32 SanitizedMax = FMath::Clamp(
		MaxActiveRadius, SanitizedMin, OceanCore::MaxSupportedChunkRadius);
	return FMath::Clamp(InActiveRadius, SanitizedMin, SanitizedMax);
}

void UOceanChunkInvokerComponent::ApplyActiveRadius(int32 InActiveRadius)
{
	const int32 NewRadius = ClampActiveRadius(InActiveRadius);
	if (ActiveRadius == NewRadius)
	{
		return;
	}

	ActiveRadius = NewRadius;
	if (IsValid(RegisteredManager))
	{
		RegisteredManager->RequestRefreshChunks();
	}
}

void UOceanChunkInvokerComponent::TickRegistration()
{
	if (IsValid(RegisteredManager))
	{
		return;
	}

	RegisteredManager = nullptr;
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (UOceanWorldManagerComponent* Manager = UOceanWorldManagerComponent::Get(this))
	{
		RegisteredManager = Manager;
		Manager->RegisterInvoker(this);
	}
}

void UOceanChunkInvokerComponent::StartRegistrationTimer()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(RegistrationTimer))
	{
		return;
	}

	const float Interval = FMath::Max(0.1f, RegistrationRetryInterval);
	World->GetTimerManager().SetTimer(
		RegistrationTimer, this, &ThisClass::TickRegistration, Interval, true);

	if (!IsValid(RegisteredManager))
	{
		UE_LOG(LogOceanCore, Verbose, TEXT("Chunk invoker %s is waiting for an OceanWorldManagerComponent."),
			*GetNameSafe(GetOwner()));
	}
}

void UOceanChunkInvokerComponent::StopRegistrationTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegistrationTimer);
	}
}
