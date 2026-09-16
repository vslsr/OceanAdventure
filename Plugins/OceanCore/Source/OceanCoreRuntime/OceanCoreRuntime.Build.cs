// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OceanCoreRuntime : ModuleRules
{
	public OceanCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// GeometryCore/GeometryFramework: the terrain chunk component is a
				// UDynamicMeshComponent, so the fill mesh and the collision body are one
				// surface rather than two that can drift apart.
				"GeometryCore",
				"GeometryFramework",
				"NetCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ModularGameplay",
				// Projects: the terrain parity test resolves its fixture through IPluginManager
				// rather than a path built from the project directory.
				"Projects"
			}
		);
	}
}
