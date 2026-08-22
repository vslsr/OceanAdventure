// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildPieceDefinition.h"
#include "Naval/NavalCoreTypes.h"

#include "NavalPieceFragments.generated.h"

/**
 * What one build piece adds to tonnage, buoyancy capacity, thrust and steering.
 *
 * Kept on the piece rather than on a spawned actor so instanced pieces -- decks, walls,
 * windows, which never become Actors -- still weigh something. Without this a player could
 * wall in the whole deck for free and the carry model would only price the cannons.
 */
UCLASS(DisplayName = "Naval Load")
class NAVALCORERUNTIME_API UBuildPieceFragment_NavalLoad final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	/** W. Design 7.11 starting scale: floor 1, wall/window 2, chest 4, big gun 12. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float Tonnage = 1.0f;

	/** C. Only pontoons contribute here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float BuoyancyCapacity = 0.0f;

	/** P. Sails, oars and engines. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float Thrust = 0.0f;

	/** Relative turn authority; rudders and side thrusters only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float Steering = 0.0f;
};

/**
 * Turns a spawned piece into a damageable naval part with its own construction window.
 *
 * Applied to the UNavalPartComponent on the spawned Actor, so a pontoon, a rudder and a
 * cannon all inherit the same deploy-windup / construction / operational chain.
 */
UCLASS(DisplayName = "Naval Part")
class NAVALCORERUNTIME_API UBuildPieceFragment_NavalPart final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Part")
	ENavalPartType PartType = ENavalPartType::Structure;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "1.0"))
	float MaxDurability = 100.0f;

	/** 部署前摇. Design 7.8 asks for 0.6-1.0 seconds on every module size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "0.0", Units = "s"))
	float DeploySeconds = 0.8f;

	/**
	 * 实体施工 window after the windup. Design 7.8 bands: light structure 0.8-1.2 total,
	 * defensive 1.5-2.5, mid-size function 2.5-4, large function 4-6.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "0.0", Units = "s"))
	float ConstructionSeconds = 1.7f;
};

/**
 * Marks a piece as a 单向射击窗 and carries the arc the ballistic rule tests against.
 *
 * The rule itself never reads the mesh: a window is authorised by team, by which side the
 * shot starts on, and by the arc. Turning collision off to "make a hole" is explicitly not
 * how this works, or enemy fire would pass through the open frame too.
 */
UCLASS(DisplayName = "Naval Fire Window")
class NAVALCORERUNTIME_API UBuildPieceFragment_NavalFireWindow final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	/** Half of the outward firing arc. Design 7.7 asks for 60-90 degrees total. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "10.0", ClampMax = "60.0", Units = "deg"))
	float OutwardHalfAngleDegrees = 40.0f;

	/** How far behind the window still counts as the authorised inner side. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "10.0", Units = "cm"))
	float InnerAuthorizedDepth = 260.0f;

	/** Shutter windup a paired weapon waits out. Design 7.7 asks for 0.15-0.25 seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "0.0", Units = "s"))
	float OpenWindupSeconds = 0.2f;
};
