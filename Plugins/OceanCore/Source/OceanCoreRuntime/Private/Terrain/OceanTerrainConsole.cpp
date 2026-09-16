// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "OceanCoreRuntimeModule.h"
#include "Terrain/OceanTerrainContent.h"
#include "Terrain/OceanTerrainEditor.h"
#include "Terrain/OceanTerrainPatchStore.h"
#include "Terrain/OceanTerrainSubsystem.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Console commands for editing terrain by hand.
 *
 * These exist so the sparse edit layer (migration plan P3) can be exercised without authoring
 * a Blueprint first. Verifying "raise one cell, watch the mesh and collision follow, and watch
 * the neighbouring chunk follow when the cell is on a border" otherwise needs a graph built
 * just to call three functions.
 *
 * Debug tooling: compiled out of Shipping. Edits apply to the local world only -- the
 * server-authoritative channel is P4 -- so in a multiplayer PIE session only the world the
 * command ran in changes.
 */
namespace
{
	// No `using namespace OceanTerrain;` here. This is a unity build: a file-scope using
	// directive leaks into every other .cpp compiled into the same blob, and names like
	// ChunkSize / CellSize / NoiseScale then collide with their local variables. The errors
	// land in files this one never touched, which is a miserable trail to follow.

	/** Prints one line to both the log and the calling console. */
	void Report(const FString& Line)
	{
		UE_LOG(LogOceanCore, Display, TEXT("%s"), *Line);
	}

	/**
	 * Resolves the subsystem and its editor, explaining the likely cause when they are missing.
	 *
	 * The store is created when the first chunk registers, so "no editor" almost always means
	 * no chunk carries a UOceanTerrainChunkComponent yet -- which is a setup step people
	 * forget, not a failure of the command.
	 */
	OceanTerrain::FTerrainEditor* ResolveEditor(UWorld* World)
	{
		UOceanTerrainSubsystem* Terrain = UOceanTerrainSubsystem::Get(World);
		if (!Terrain)
		{
			Report(TEXT("Ocean.Terrain: no terrain subsystem in this world."));
			return nullptr;
		}

		OceanTerrain::FTerrainEditor* Editor = Terrain->GetEditor();
		if (!Editor)
		{
			Report(TEXT(
				"Ocean.Terrain: terrain store not initialised. It is created when the first "
				"chunk registers, so check that the class in BP_OceanWorldManager's ChunkClass "
				"carries an OceanTerrainChunk component."));
			return nullptr;
		}
		return Editor;
	}

	/** Cell under the local player pawn, or the view target if there is no pawn. */
	bool ResolveHereCell(UWorld* World, int32& OutCellX, int32& OutCellY)
	{
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const AActor* Target = Controller
			? (Controller->GetPawn() ? static_cast<const AActor*>(Controller->GetPawn())
									 : static_cast<const AActor*>(Controller->GetViewTarget()))
			: nullptr;
		if (!Target)
		{
			Report(TEXT("Ocean.Terrain: no local player to take a position from. "
						"Press Play, or pass cell coordinates explicitly."));
			return false;
		}

		const FVector Location = Target->GetActorLocation();
		OutCellX = OceanTerrain::WorldToCell(Location.X);
		OutCellY = OceanTerrain::WorldToCell(Location.Y);
		return true;
	}

	bool ParseCell(const TArray<FString>& Args, int32 Index, int32& OutCellX, int32& OutCellY)
	{
		if (Args.Num() < Index + 2)
		{
			Report(TEXT("Ocean.Terrain: expected cell coordinates 'X Y'. "
						"Use Ocean.Terrain.Here to find the cell you are standing on."));
			return false;
		}
		OutCellX = FCString::Atoi(*Args[Index]);
		OutCellY = FCString::Atoi(*Args[Index + 1]);
		return true;
	}

	int32 ParseSteps(const TArray<FString>& Args, int32 Index)
	{
		const int32 Steps = Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : 1;
		return FMath::Max(1, Steps);
	}

	void ReportCell(OceanTerrain::FTerrainEditor& Editor, int32 CellX, int32 CellY, const TCHAR* Prefix)
	{
		const OceanTerrain::FTerrainCellView View = Editor.ReadCell(CellX, CellY);
		Report(FString::Printf(
			TEXT("%s cell (%d, %d): level=%d shape=%s surface=%s biome=%s bedZ=%.0f depth=%.0f %s"),
			Prefix,
			CellX,
			CellY,
			View.HeightLevel,
			*UEnum::GetValueAsString(View.Shape),
			*UEnum::GetValueAsString(View.Surface),
			*UEnum::GetValueAsString(View.Biome),
			View.BedZ,
			View.WaterDepth,
			View.bPatched ? TEXT("[edited]") : TEXT("[procedural]")));
	}

	/** Shared body for the edits that take a cell plus an optional step count. */
	void RunCellEdit(
		UWorld* World,
		const TArray<FString>& Args,
		bool bHere,
		TFunctionRef<bool(OceanTerrain::FTerrainEditor&, int32, int32, int32)> Apply,
		const TCHAR* Verb)
	{
		OceanTerrain::FTerrainEditor* Editor = ResolveEditor(World);
		if (!Editor)
		{
			return;
		}

		int32 CellX = 0;
		int32 CellY = 0;
		int32 Steps = 1;
		if (bHere)
		{
			if (!ResolveHereCell(World, CellX, CellY))
			{
				return;
			}
			Steps = ParseSteps(Args, 0);
		}
		else
		{
			if (!ParseCell(Args, 0, CellX, CellY))
			{
				return;
			}
			Steps = ParseSteps(Args, 2);
		}

		const bool bChanged = Apply(*Editor, CellX, CellY, Steps);
		Report(FString::Printf(
			TEXT("Ocean.Terrain: %s cell (%d, %d) -> %s"),
			Verb,
			CellX,
			CellY,
			bChanged ? TEXT("changed") : TEXT("no change")));
		ReportCell(*Editor, CellX, CellY, TEXT("Ocean.Terrain: now"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainHere(
	TEXT("Ocean.Terrain.Here"),
	TEXT("Prints the terrain cell under the local player, and that cell's current state."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			OceanTerrain::FTerrainEditor* Editor = ResolveEditor(World);
			int32 CellX = 0;
			int32 CellY = 0;
			if (!Editor || !ResolveHereCell(World, CellX, CellY))
			{
				return;
			}
			ReportCell(*Editor, CellX, CellY, TEXT("Ocean.Terrain: standing on"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainInfo(
	TEXT("Ocean.Terrain.Info"),
	TEXT("Ocean.Terrain.Info X Y -- prints one cell's height, shape, surface, biome and depth."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			OceanTerrain::FTerrainEditor* Editor = ResolveEditor(World);
			int32 CellX = 0;
			int32 CellY = 0;
			if (!Editor || !ParseCell(Args, 0, CellX, CellY))
			{
				return;
			}
			ReportCell(*Editor, CellX, CellY, TEXT("Ocean.Terrain:"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainRaise(
	TEXT("Ocean.Terrain.Raise"),
	TEXT("Ocean.Terrain.Raise X Y [Steps] -- raises one cell."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, false,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32 Steps)
				{ return Editor.Raise(X, Y, Steps); },
				TEXT("raise"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainLower(
	TEXT("Ocean.Terrain.Lower"),
	TEXT("Ocean.Terrain.Lower X Y [Steps] -- lowers one cell."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, false,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32 Steps)
				{ return Editor.Lower(X, Y, Steps); },
				TEXT("lower"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainRaiseHere(
	TEXT("Ocean.Terrain.RaiseHere"),
	TEXT("Ocean.Terrain.RaiseHere [Steps] -- raises the cell under the local player."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, true,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32 Steps)
				{ return Editor.Raise(X, Y, Steps); },
				TEXT("raise"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainLowerHere(
	TEXT("Ocean.Terrain.LowerHere"),
	TEXT("Ocean.Terrain.LowerHere [Steps] -- lowers the cell under the local player."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, true,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32 Steps)
				{ return Editor.Lower(X, Y, Steps); },
				TEXT("lower"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainFlatten(
	TEXT("Ocean.Terrain.Flatten"),
	TEXT("Ocean.Terrain.Flatten X Y -- removes a cell's ramp or corner shape."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, false,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32) { return Editor.Flatten(X, Y); },
				TEXT("flatten"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainFlood(
	TEXT("Ocean.Terrain.Flood"),
	TEXT("Ocean.Terrain.Flood X Y -- floods a cell, dropping it until it holds real depth."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, false,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32) { return Editor.Flood(X, Y); },
				TEXT("flood"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainReset(
	TEXT("Ocean.Terrain.Reset"),
	TEXT("Ocean.Terrain.Reset X Y -- drops one cell's override, back to the procedural value."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			RunCellEdit(World, Args, false,
				[](OceanTerrain::FTerrainEditor& Editor, int32 X, int32 Y, int32) { return Editor.ResetCell(X, Y); },
				TEXT("reset"));
		}));

static FAutoConsoleCommandWithWorldAndArgs GOceanTerrainResetAll(
	TEXT("Ocean.Terrain.ResetAll"),
	TEXT("Ocean.Terrain.ResetAll -- drops every terrain override in this world."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UOceanTerrainSubsystem* Terrain = UOceanTerrainSubsystem::Get(World);
			OceanTerrain::FTerrainEditor* Editor = ResolveEditor(World);
			OceanTerrain::FTerrainPatchStore* Store = Terrain ? Terrain->GetStore() : nullptr;
			if (!Editor || !Store)
			{
				return;
			}

			// Reset cell by cell rather than clearing the store wholesale: clearing skips the
			// change notification, so the chunks would keep drawing the edits that are no
			// longer there.
			TArray<int32> Triples;
			Store->Entries(Triples);
			int32 Cleared = 0;
			for (int32 Offset = 0; Offset + 2 < Triples.Num(); Offset += 3)
			{
				Cleared += Editor->ResetCell(Triples[Offset], Triples[Offset + 1]) ? 1 : 0;
			}
			Report(FString::Printf(
				TEXT("Ocean.Terrain: cleared %d edited cell(s); %d remain."),
				Cleared,
				Store->Num()));
		}));

#endif // !UE_BUILD_SHIPPING
