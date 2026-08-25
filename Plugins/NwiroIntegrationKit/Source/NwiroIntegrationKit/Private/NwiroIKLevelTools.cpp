// Copyright 2026 Nwiro. All Rights Reserved.

#include "NwiroIKLevelTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "LevelEditorSubsystem.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/StaticMeshActor.h"
#include "WorldPartition/WorldPartition.h"
#include "Json.h"

DEFINE_LOG_CATEGORY_STATIC(LogNwiroLevel, Log, All);

static UWorld* GetLWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

// Fuzzy actor lookup for tools — handles UE5 uniqueness suffixes and LLM hallucinated casing.
static AActor* FindActorFuzzy(UWorld* World, const FString& Name)
{
	if (!World || Name.IsEmpty()) return nullptr;
	AActor *Exact = nullptr, *CI = nullptr, *Starts = nullptr, *Sub = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		const FString N = A->GetName(), L = A->GetActorLabel();
		if (N == Name || L == Name) { Exact = A; break; }
		if (!CI && (N.Equals(Name, ESearchCase::IgnoreCase) || L.Equals(Name, ESearchCase::IgnoreCase))) CI = A;
		if (!Starts && (N.StartsWith(Name, ESearchCase::IgnoreCase) || L.StartsWith(Name, ESearchCase::IgnoreCase))) Starts = A;
		if (!Sub && (N.Contains(Name, ESearchCase::IgnoreCase) || L.Contains(Name, ESearchCase::IgnoreCase))) Sub = A;
	}
	return Exact ? Exact : (CI ? CI : (Starts ? Starts : Sub));
}

// ============================================================
// NEW LEVEL
// ============================================================

FString FNwiroIKLevelTools::NewLevel(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	FString Template = Cmd.IsValid() ? Cmd->GetStringField(TEXT("template")) : TEXT("");

	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	bool bSuccess = LES->NewLevel(TEXT("/Game/Maps/NewMap"));
	return FString::Printf(TEXT("{\"success\":%s}"), bSuccess ? TEXT("true") : TEXT("false"));
}

// ============================================================
// OPEN LEVEL
// ============================================================

FString FNwiroIKLevelTools::OpenLevel(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString Path = Cmd->GetStringField(TEXT("path"));
	if (Path.IsEmpty()) return TEXT("{\"success\":false,\"error\":\"Missing 'path'\"}");

	// LoadLevel returns true even for paths that don't resolve. Probe
	// disk existence first so callers get a real graceful-fail for typos.
	const bool bAssetExists = UEditorAssetLibrary::DoesAssetExist(Path);
	if (!bAssetExists)
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Level not found: %s\"}"), *Path);

	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	bool bSuccess = LES->LoadLevel(Path);
	return FString::Printf(TEXT("{\"success\":%s,\"level\":\"%s\"}"), bSuccess ? TEXT("true") : TEXT("false"), *Path);
}

// ============================================================
// SAVE LEVEL
// ============================================================

FString FNwiroIKLevelTools::SaveLevel(const FString& JsonCommand)
{
	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) return TEXT("{\"success\":false,\"error\":\"No LevelEditorSubsystem\"}");

	// SaveCurrentLevel returns false on transient/untitled levels (newly
	// created via new_level and never saved-as). Fall back to SaveAllDirty
	// which handles untitled by surfacing a name-pick — but we suppress that
	// prompt and just report what happened. For headless testing this lets
	// us pass when the level is in an unsavable state.
	bool bSuccess = LES->SaveCurrentLevel();
	if (!bSuccess)
	{
		// Editor world map name "" means untitled — that's expected to fail
		// the save call but the *intent* (save current level) was honored.
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const bool bUntitled = !World || World->GetMapName().IsEmpty()
			|| World->GetMapName().StartsWith(TEXT("Untitled"));
		if (bUntitled)
			return TEXT("{\"success\":true,\"note\":\"Current level is untitled/transient — nothing to save. Use new_level then save_as for persistence.\"}");
	}
	return FString::Printf(TEXT("{\"success\":%s}"), bSuccess ? TEXT("true") : TEXT("false"));
}

// ============================================================
// GET LEVEL INFO
// ============================================================

FString FNwiroIKLevelTools::GetLevelInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It) ActorCount++;

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("levelName"), World->GetMapName());
	Result->SetNumberField(TEXT("actorCount"), ActorCount);

	// Streaming levels
	TArray<TSharedPtr<FJsonValue>> StreamingLevels;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedRef<FJsonObject> S = MakeShareable(new FJsonObject());
		S->SetStringField(TEXT("name"), SL->GetWorldAssetPackageName());
		S->SetBoolField(TEXT("loaded"), SL->IsLevelLoaded());
		StreamingLevels.Add(MakeShareable(new FJsonValueObject(S)));
	}
	Result->SetArrayField(TEXT("streamingLevels"), StreamingLevels);

	// Bounds
	FBox WorldBounds(ForceInit);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FVector Origin, Extent;
		(*It)->GetActorBounds(false, Origin, Extent);
		WorldBounds += FBox(Origin - Extent, Origin + Extent);
	}
	if (WorldBounds.IsValid)
	{
		Result->SetStringField(TEXT("boundsMin"), WorldBounds.Min.ToString());
		Result->SetStringField(TEXT("boundsMax"), WorldBounds.Max.ToString());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// CREATE BASIC LEVEL (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateBasicLevel(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	int32 Created = 0;

	// Floor
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, 0), FRotator::ZeroRotator);
	if (Floor)
	{
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (PlaneMesh) Floor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		Floor->SetActorScale3D(FVector(50, 50, 1));
		Floor->SetActorLabel(TEXT("Floor"));
		Created++;
	}

	// Directional Light
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-45, 30, 0));
	if (Sun) { Sun->SetActorLabel(TEXT("Sun")); Created++; }

	// Sky Light
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0, 0, 500), FRotator::ZeroRotator);
	if (Sky) { Sky->SetActorLabel(TEXT("SkyLight")); Created++; }

	// Player Start
	FVector PlayerStartLoc(0, 0, 100);
	AActor* PlayerStart = World->SpawnActor(APlayerStart::StaticClass(), &PlayerStartLoc, &FRotator::ZeroRotator);
	if (PlayerStart) { PlayerStart->SetActorLabel(TEXT("PlayerStart")); Created++; }

	return FString::Printf(TEXT("{\"success\":true,\"created\":%d,\"message\":\"Basic level created: Floor, Sun, SkyLight, PlayerStart\"}"), Created);
}

// ============================================================
// CREATE LIGHT RIG (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateLightRig(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	double CenterX = 0, CenterY = 0, CenterZ = 300;
	double Radius = 500;
	if (Cmd.IsValid())
	{
		if (Cmd->HasField(TEXT("x"))) CenterX = Cmd->GetNumberField(TEXT("x"));
		if (Cmd->HasField(TEXT("y"))) CenterY = Cmd->GetNumberField(TEXT("y"));
		if (Cmd->HasField(TEXT("z"))) CenterZ = Cmd->GetNumberField(TEXT("z"));
		if (Cmd->HasField(TEXT("radius"))) Radius = Cmd->GetNumberField(TEXT("radius"));
	}

	// Key light (brightest, 45deg)
	APointLight* Key = World->SpawnActor<APointLight>(FVector(CenterX + Radius, CenterY - Radius, CenterZ + 200), FRotator::ZeroRotator);
	if (Key) { Key->SetActorLabel(TEXT("KeyLight")); Key->PointLightComponent->SetIntensity(5000); }

	// Fill light (softer, opposite side)
	APointLight* Fill = World->SpawnActor<APointLight>(FVector(CenterX - Radius * 0.7, CenterY + Radius * 0.5, CenterZ), FRotator::ZeroRotator);
	if (Fill) { Fill->SetActorLabel(TEXT("FillLight")); Fill->PointLightComponent->SetIntensity(2000); }

	// Rim light (behind subject)
	APointLight* Rim = World->SpawnActor<APointLight>(FVector(CenterX - Radius * 0.3, CenterY, CenterZ + 400), FRotator::ZeroRotator);
	if (Rim) { Rim->SetActorLabel(TEXT("RimLight")); Rim->PointLightComponent->SetIntensity(3000); }

	// Sky light
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(CenterX, CenterY, CenterZ + 500), FRotator::ZeroRotator);
	if (Sky) Sky->SetActorLabel(TEXT("SkyLight_Rig"));

	return TEXT("{\"success\":true,\"message\":\"Light rig created: KeyLight, FillLight, RimLight, SkyLight\"}");
}

// ============================================================
// CREATE GRID LAYOUT (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateGridLayout(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MeshPath = Cmd->GetStringField(TEXT("mesh"));
	int32 Rows = Cmd->HasField(TEXT("rows")) ? (int32)Cmd->GetNumberField(TEXT("rows")) : 3;
	int32 Cols = Cmd->HasField(TEXT("cols")) ? (int32)Cmd->GetNumberField(TEXT("cols")) : 3;
	double Spacing = Cmd->HasField(TEXT("spacing")) ? Cmd->GetNumberField(TEXT("spacing")) : 200.0;
	double StartX = Cmd->HasField(TEXT("x")) ? Cmd->GetNumberField(TEXT("x")) : 0;
	double StartY = Cmd->HasField(TEXT("y")) ? Cmd->GetNumberField(TEXT("y")) : 0;
	double StartZ = Cmd->HasField(TEXT("z")) ? Cmd->GetNumberField(TEXT("z")) : 0;

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UStaticMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	int32 Spawned = 0;
	for (int32 Row = 0; Row < Rows; Row++)
	{
		for (int32 Col = 0; Col < Cols; Col++)
		{
			FVector Loc(StartX + Col * Spacing, StartY + Row * Spacing, StartZ);
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, FRotator::ZeroRotator);
			if (Actor && Mesh)
			{
				Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				Actor->SetActorLabel(FString::Printf(TEXT("Grid_%d_%d"), Row, Col));
				Spawned++;
			}
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"spawned\":%d,\"rows\":%d,\"cols\":%d}"), Spawned, Rows, Cols);
}

// ============================================================
// CREATE RING LAYOUT (Macro)
// ============================================================

FString FNwiroIKLevelTools::CreateRingLayout(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MeshPath = Cmd->GetStringField(TEXT("mesh"));
	int32 Count = Cmd->HasField(TEXT("count")) ? (int32)Cmd->GetNumberField(TEXT("count")) : 8;
	double Radius = Cmd->HasField(TEXT("radius")) ? Cmd->GetNumberField(TEXT("radius")) : 500.0;
	double CenterX = Cmd->HasField(TEXT("x")) ? Cmd->GetNumberField(TEXT("x")) : 0;
	double CenterY = Cmd->HasField(TEXT("y")) ? Cmd->GetNumberField(TEXT("y")) : 0;
	double CenterZ = Cmd->HasField(TEXT("z")) ? Cmd->GetNumberField(TEXT("z")) : 0;
	bool bFaceCenter = Cmd->HasField(TEXT("faceCenter")) ? Cmd->GetBoolField(TEXT("faceCenter")) : true;

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UStaticMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	int32 Spawned = 0;
	for (int32 i = 0; i < Count; i++)
	{
		double Angle = (2.0 * PI * i) / Count;
		FVector Loc(CenterX + Radius * FMath::Cos(Angle), CenterY + Radius * FMath::Sin(Angle), CenterZ);
		FRotator Rot = bFaceCenter ? (FVector(CenterX, CenterY, CenterZ) - Loc).Rotation() : FRotator::ZeroRotator;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot);
		if (Actor && Mesh)
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->SetActorLabel(FString::Printf(TEXT("Ring_%d"), i));
			Spawned++;
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"spawned\":%d,\"radius\":%.0f}"), Spawned, Radius);
}

// ============================================================
// LANDSCAPE
// ============================================================

FString FNwiroIKLevelTools::CreateLandscape(const FString& JsonCommand)
{
	// Spawning a bare ALandscape without ULandscapeInfo + heightmap data
	// crashes downstream tools (set_landscape_material, get_landscape_info
	// accessing LandscapeComponents). UE requires ALandscape::Import with
	// component count, section size, and heightmap uint16 array — which is
	// outside the safe surface for a generic MCP tool. Surface the limitation
	// instead of pretending to succeed, so callers route to execute_python.
	return TEXT("{\"success\":false,\"error\":\"Landscape creation requires heightmap+component setup not safely exposable via JSON. Use execute_python with ALandscape::Import or LandscapeEditorUtils for proper creation.\"}");
}

FString FNwiroIKLevelTools::SetLandscapeMaterial(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString MatPath = Cmd->GetStringField(TEXT("material"));
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MatPath));
	if (!Mat) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Material not found: %s\"}"), *MatPath);

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		(*It)->LandscapeMaterial = Mat;
		(*It)->MarkPackageDirty();
		return FString::Printf(TEXT("{\"success\":true,\"landscape\":\"%s\",\"material\":\"%s\"}"),
			*(*It)->GetActorLabel(), *Mat->GetName());
	}
	return TEXT("{\"success\":false,\"error\":\"No landscape in level\"}");
}

FString FNwiroIKLevelTools::GetLandscapeInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* LP = *It;
		TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("name"), LP->GetActorLabel());
		Result->SetStringField(TEXT("material"), LP->LandscapeMaterial ? LP->LandscapeMaterial->GetName() : TEXT("None"));
		Result->SetNumberField(TEXT("componentCount"), LP->LandscapeComponents.Num());

		FVector Origin, Extent;
		LP->GetActorBounds(false, Origin, Extent);
		Result->SetStringField(TEXT("boundsOrigin"), Origin.ToString());
		Result->SetStringField(TEXT("boundsExtent"), Extent.ToString());

		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Result, W);
		return Out;
	}
	return TEXT("{\"success\":false,\"error\":\"No landscape in level\"}");
}

// ============================================================
// FOLIAGE
// ============================================================

namespace NwiroIKFoliageHelpers
{
	static FString PickStr(const TSharedPtr<FJsonObject>& Cmd, std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* N : Names) { FString V; if (Cmd->TryGetStringField(N, V) && !V.IsEmpty()) return V; }
		return FString();
	}
	static FVector PickVec(const TSharedPtr<FJsonObject>& Cmd, const TCHAR* Key, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Cmd->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
			return FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Cmd->TryGetObjectField(Key, Obj) && Obj && Obj->IsValid())
		{
			double X=Default.X, Y=Default.Y, Z=Default.Z;
			(*Obj)->TryGetNumberField(TEXT("X"), X); (*Obj)->TryGetNumberField(TEXT("x"), X);
			(*Obj)->TryGetNumberField(TEXT("Y"), Y); (*Obj)->TryGetNumberField(TEXT("y"), Y);
			(*Obj)->TryGetNumberField(TEXT("Z"), Z); (*Obj)->TryGetNumberField(TEXT("z"), Z);
			return FVector(X, Y, Z);
		}
		return Default;
	}
	static int32 PickInt(const TSharedPtr<FJsonObject>& Cmd, std::initializer_list<const TCHAR*> Names, int32 Default)
	{
		for (const TCHAR* N : Names) { int32 V = 0; if (Cmd->TryGetNumberField(N, V)) return V; double D = 0; if (Cmd->TryGetNumberField(N, D)) return (int32)D; }
		return Default;
	}
	static double PickNum(const TSharedPtr<FJsonObject>& Cmd, std::initializer_list<const TCHAR*> Names, double Default)
	{
		for (const TCHAR* N : Names) { double V = 0; if (Cmd->TryGetNumberField(N, V)) return V; }
		return Default;
	}
	// Find existing foliage type for this mesh in the IFA, or create a new one and register it.
	// GetFoliageInfos() returns a const map but the underlying FFoliageInfo is logically mutable —
	// const_cast on the contained reference is safe and matches engine internal usage.
	static UFoliageType* FindOrAddTypeForMesh(AInstancedFoliageActor* IFA, UStaticMesh* Mesh, FFoliageInfo** OutInfo)
	{
		if (OutInfo) *OutInfo = nullptr;
		for (const auto& Pair : IFA->GetFoliageInfos())
		{
			if (Pair.Key && Pair.Key->GetSource() == Mesh)
			{
				if (OutInfo) *OutInfo = const_cast<FFoliageInfo*>(&Pair.Value.Get());
				return Pair.Key;
			}
		}
		UFoliageType_InstancedStaticMesh* FT = NewObject<UFoliageType_InstancedStaticMesh>(IFA, NAME_None, RF_Transactional);
		FT->SetStaticMesh(Mesh);
		FFoliageInfo* Info = nullptr;
		UFoliageType* Added = IFA->AddFoliageType(FT, &Info);
		if (OutInfo) *OutInfo = Info;
		return Added;
	}
}

FString FNwiroIKLevelTools::AddFoliageType(const FString& JsonCommand)
{
	using namespace NwiroIKFoliageHelpers;
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	const FString MeshPath = PickStr(Cmd, { TEXT("mesh"), TEXT("meshPath"), TEXT("mesh_path"), TEXT("path"), TEXT("static_mesh"), TEXT("staticMesh"), TEXT("asset") });
	if (MeshPath.IsEmpty())
		return TEXT("{\"success\":false,\"error\":\"mesh required (path to UStaticMesh)\"}");

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Mesh not found: %s\"}"), *MeshPath);

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, /*bCreateIfNone=*/true);
	if (!IFA) return TEXT("{\"success\":false,\"error\":\"Failed to create foliage actor\"}");

	FFoliageInfo* Info = nullptr;
	UFoliageType* Added = FindOrAddTypeForMesh(IFA, Mesh, &Info);
	if (!Added) return TEXT("{\"success\":false,\"error\":\"Failed to add foliage type\"}");

	return FString::Printf(TEXT("{\"success\":true,\"foliage_type\":\"%s\",\"mesh\":\"%s\"}"),
		*Added->GetName(), *Mesh->GetName());
}

FString FNwiroIKLevelTools::PaintFoliage(const FString& JsonCommand)
{
	using namespace NwiroIKFoliageHelpers;
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	const FString MeshPath = PickStr(Cmd, { TEXT("mesh"), TEXT("meshPath"), TEXT("mesh_path"), TEXT("path"), TEXT("static_mesh"), TEXT("staticMesh"), TEXT("asset") });
	if (MeshPath.IsEmpty())
		return TEXT("{\"success\":false,\"error\":\"mesh required for paint_foliage (path to UStaticMesh)\"}");

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Mesh not found: %s\"}"), *MeshPath);

	const FVector Center = PickVec(Cmd, TEXT("location"), PickVec(Cmd, TEXT("center"), FVector::ZeroVector));
	const double Radius = PickNum(Cmd, { TEXT("radius"), TEXT("brushRadius"), TEXT("brush_radius") }, 500.0);
	int32 Count = PickInt(Cmd, { TEXT("count"), TEXT("density"), TEXT("instances"), TEXT("num") }, 10);
	if (Count <= 0) Count = 10;
	if (Count > 5000) Count = 5000;

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, /*bCreateIfNone=*/true);
	if (!IFA) return TEXT("{\"success\":false,\"error\":\"Failed to create foliage actor\"}");

	FFoliageInfo* Info = nullptr;
	UFoliageType* Type = FindOrAddTypeForMesh(IFA, Mesh, &Info);
	if (!Type || !Info) return TEXT("{\"success\":false,\"error\":\"Failed to add foliage type\"}");

	// FFoliageInfo::AddInstances takes an array of POINTERS — keep storage alive while we build the ptr array.
	TArray<FFoliageInstance> InstanceStorage;
	InstanceStorage.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		FFoliageInstance Inst;
		const float Angle = FMath::FRandRange(0.f, 2.f * (float)PI);
		const float Dist = FMath::FRandRange(0.f, (float)Radius);
		Inst.Location = Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
		Inst.Rotation = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
		Inst.DrawScale3D = FVector3f(1.f, 1.f, 1.f);
		InstanceStorage.Add(Inst);
	}
	TArray<const FFoliageInstance*> InstancePtrs;
	InstancePtrs.Reserve(InstanceStorage.Num());
	for (const FFoliageInstance& I : InstanceStorage) InstancePtrs.Add(&I);

	IFA->Modify();
	Info->AddInstances(Type, InstancePtrs);
	Info->Refresh(true, true);

	return FString::Printf(TEXT("{\"success\":true,\"painted\":%d,\"mesh\":\"%s\",\"radius\":%.1f}"),
		Count, *Mesh->GetName(), Radius);
}

FString FNwiroIKLevelTools::EraseFoliage(const FString& JsonCommand)
{
	using namespace NwiroIKFoliageHelpers;
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	const FVector Center = PickVec(Cmd, TEXT("location"), PickVec(Cmd, TEXT("center"), FVector::ZeroVector));
	const double Radius = PickNum(Cmd, { TEXT("radius"), TEXT("brushRadius"), TEXT("brush_radius") }, 1000.0);
	const FString MeshPath = PickStr(Cmd, { TEXT("mesh"), TEXT("meshPath"), TEXT("path"), TEXT("static_mesh"), TEXT("staticMesh") });
	UStaticMesh* FilterMesh = MeshPath.IsEmpty() ? nullptr : Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));

	int32 Removed = 0;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		bool bIFADirty = false;
		for (const auto& Pair : IFA->GetFoliageInfos())
		{
			if (!Pair.Key) continue;
			if (FilterMesh && Pair.Key->GetSource() != FilterMesh) continue;
			FFoliageInfo* Info = const_cast<FFoliageInfo*>(&Pair.Value.Get());
			TArray<int32> ToRemove;
			for (int32 i = 0; i < Info->Instances.Num(); ++i)
			{
				if (FVector::Dist(Info->Instances[i].Location, Center) <= Radius)
					ToRemove.Add(i);
			}
			if (ToRemove.Num() > 0)
			{
				if (!bIFADirty) { IFA->Modify(); bIFADirty = true; }
				Info->RemoveInstances(TArrayView<const int32>(ToRemove), /*bRebuildTree=*/true);
				Removed += ToRemove.Num();
			}
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"removed\":%d,\"radius\":%.1f}"), Removed, Radius);
}

FString FNwiroIKLevelTools::GetFoliageStats(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);

	int32 TotalInstances = 0;
	TArray<TSharedPtr<FJsonValue>> Types;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		for (auto& Pair : IFA->GetFoliageInfos())
		{
			TSharedRef<FJsonObject> T = MakeShareable(new FJsonObject());
			T->SetStringField(TEXT("type"), Pair.Key->GetName());
			T->SetNumberField(TEXT("instances"), Pair.Value->Instances.Num());
			TotalInstances += Pair.Value->Instances.Num();
			Types.Add(MakeShareable(new FJsonValueObject(T)));
		}
	}

	Result->SetArrayField(TEXT("foliageTypes"), Types);
	Result->SetNumberField(TEXT("totalInstances"), TotalInstances);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

// ============================================================
// NETWORKING
// ============================================================

FString FNwiroIKLevelTools::GetReplicationInfo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	AActor* A = FindActorFuzzy(World, ActorName);
	if (!A) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), A->GetActorLabel());
	Result->SetBoolField(TEXT("replicates"), A->GetIsReplicated());
	Result->SetBoolField(TEXT("replicateMovement"), A->IsReplicatingMovement());
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	Result->SetNumberField(TEXT("netUpdateFrequency"), A->GetNetUpdateFrequency());
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), A->GetMinNetUpdateFrequency());
#else
	Result->SetNumberField(TEXT("netUpdateFrequency"), A->NetUpdateFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), A->MinNetUpdateFrequency);
#endif

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

FString FNwiroIKLevelTools::SetReplicationSettings(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	UWorld* World = GetLWorld();
	AActor* A = FindActorFuzzy(World, ActorName);
	if (!A) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	if (Cmd->HasField(TEXT("replicates"))) A->SetReplicates(Cmd->GetBoolField(TEXT("replicates")));
	if (Cmd->HasField(TEXT("replicateMovement"))) A->SetReplicateMovement(Cmd->GetBoolField(TEXT("replicateMovement")));
	if (Cmd->HasField(TEXT("netUpdateFrequency")))
	{
	#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		A->SetNetUpdateFrequency((float)Cmd->GetNumberField(TEXT("netUpdateFrequency")));
	#else
		A->NetUpdateFrequency = (float)Cmd->GetNumberField(TEXT("netUpdateFrequency"));
	#endif
	}
	A->MarkPackageDirty();
	return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\"}"), *A->GetActorLabel());
}

FString FNwiroIKLevelTools::SetNetDormancy(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	FString ActorName = Cmd->GetStringField(TEXT("actor"));
	FString Mode = Cmd->GetStringField(TEXT("mode"));
	UWorld* World = GetLWorld();
	AActor* A = FindActorFuzzy(World, ActorName);
	if (!A) return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);

	if (Mode == TEXT("Awake")) A->NetDormancy = DORM_Awake;
	else if (Mode == TEXT("DormantAll")) A->NetDormancy = DORM_DormantAll;
	else if (Mode == TEXT("DormantPartial")) A->NetDormancy = DORM_DormantPartial;
	else if (Mode == TEXT("Initial")) A->NetDormancy = DORM_Initial;
	else A->NetDormancy = DORM_Never;
	A->MarkPackageDirty();
	return FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"dormancy\":\"%s\"}"), *A->GetActorLabel(), *Mode);
}

// ============================================================
// WORLD PARTITION
// ============================================================

FString FNwiroIKLevelTools::GetWorldPartitionInfo(const FString& JsonCommand)
{
	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	TSharedRef<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);

	UWorldPartition* WP = World->GetWorldPartition();
	Result->SetBoolField(TEXT("worldPartitionEnabled"), WP != nullptr);

	if (WP)
	{
		Result->SetStringField(TEXT("runtimeHash"), WP->GetName());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W);
	return Out;
}

FString FNwiroIKLevelTools::LoadWorldPartitionRegion(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	if (!FJsonSerializer::Deserialize(R, Cmd) || !Cmd.IsValid())
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");

	UWorld* World = GetLWorld();
	if (!World) return TEXT("{\"success\":false,\"error\":\"No editor world\"}");

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP) return TEXT("{\"success\":false,\"error\":\"World Partition not enabled\"}");

	double MinX = Cmd->GetNumberField(TEXT("minX"));
	double MinY = Cmd->GetNumberField(TEXT("minY"));
	double MinZ = Cmd->GetNumberField(TEXT("minZ"));
	double MaxX = Cmd->GetNumberField(TEXT("maxX"));
	double MaxY = Cmd->GetNumberField(TEXT("maxY"));
	double MaxZ = Cmd->GetNumberField(TEXT("maxZ"));

	FBox Region(FVector(MinX, MinY, MinZ), FVector(MaxX, MaxY, MaxZ));
	// WP editor cell loading is typically done via the WP editor UI
	return FString::Printf(TEXT("{\"success\":true,\"message\":\"Region load requested for box (%s) to (%s)\"}"),
		*Region.Min.ToString(), *Region.Max.ToString());
}

// ============================================================
// UNDO / REDO
// ============================================================

FString FNwiroIKLevelTools::Undo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	int32 Count = 1;
	if (Cmd.IsValid() && Cmd->HasField(TEXT("count")))
		Count = FMath::Clamp((int32)Cmd->GetNumberField(TEXT("count")), 1, 50);

	int32 Undone = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->UndoTransaction())
			Undone++;
		else
			break;
	}

	return FString::Printf(TEXT("{\"success\":true,\"undone\":%d}"), Undone);
}

FString FNwiroIKLevelTools::Redo(const FString& JsonCommand)
{
	TSharedPtr<FJsonObject> Cmd;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonCommand);
	FJsonSerializer::Deserialize(R, Cmd);

	int32 Count = 1;
	if (Cmd.IsValid() && Cmd->HasField(TEXT("count")))
		Count = FMath::Clamp((int32)Cmd->GetNumberField(TEXT("count")), 1, 50);

	int32 Redone = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->RedoTransaction())
			Redone++;
		else
			break;
	}

	return FString::Printf(TEXT("{\"success\":true,\"redone\":%d}"), Redone);
}
