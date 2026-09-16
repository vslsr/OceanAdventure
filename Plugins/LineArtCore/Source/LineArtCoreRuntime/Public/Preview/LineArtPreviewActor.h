// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "LineArtPreviewActor.generated.h"

class ULineArtPointLightComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Drop one in a level to see the whole line-art style at once.
 *
 * The style is not a single material you can tick on a mesh -- it is a flat fill PLUS a
 * second, inverted-hull draw of the same geometry. Assembling that by hand every time you
 * want to look at something is tedious enough that people stop looking, so this actor owns
 * both components and keeps them in step.
 *
 * It is also the reference wiring for the HISM batches that come next: same two draws, same
 * two materials resolved from ULineArtCoreSettings, same per-instance base colour.
 *
 * Two things worth knowing before the first look:
 *
 *   - **Use a smooth-normal mesh.** The default sphere is one. A cube's outline splits open
 *     at every crease, which is not a bug in this actor: extruding along hard-edged normals
 *     has nowhere to go at a corner. Production meshes need averaged normals baked in at
 *     authoring time; until then, round things preview correctly and boxes do not.
 *   - **Outside PIE the environment subsystem does not tick**, so the collection holds its
 *     defaults and nothing animates. That is what makes the editor viewport useful for this:
 *     open MPC_LineArtEnvironment and scrub InkColor or Daylight, and the whole level answers
 *     live. Press Play to see day/night and firelight drive the same values on their own.
 */
UCLASS(Blueprintable, ClassGroup = (LineArt))
class LINEARTCORERUNTIME_API ALineArtPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ALineArtPreviewActor();

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Geometry drawn by both passes. Defaults to the engine sphere so a fresh drop shows something. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	TObjectPtr<UStaticMesh> PreviewMesh;

	/** Feeds the fill material's BaseColor parameter; stands in for per-instance tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	FLinearColor FillColor = FLinearColor(0.82f, 0.78f, 0.70f);

	/** Turn off to A/B the fill on its own. The single most useful control here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	bool bShowOutline = true;

	/** Turn off for a preview that is only about fill and outline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	bool bShowPointLight = true;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Line Art|Preview")
	void RebuildPreview();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	TObjectPtr<UStaticMeshComponent> FillMesh;

	/**
	 * The hull. Same geometry, outline material, pushed out along the vertex normal by the
	 * material itself -- there is no offset on this component, and giving it one would fight
	 * the screen-space thickness the material computes.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	TObjectPtr<UStaticMeshComponent> OutlineMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Line Art|Preview")
	TObjectPtr<ULineArtPointLightComponent> PointLight;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FillMaterialInstance;
};
