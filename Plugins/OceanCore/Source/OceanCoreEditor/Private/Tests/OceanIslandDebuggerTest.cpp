// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Widgets/SOceanIslandDebugger.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOceanIslandDebuggerDefaultPreviewTest,
	"OceanCore.Editor.IslandDebuggerDefaultPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOceanIslandDebuggerDefaultPreviewTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SOceanIslandDebugger> Debugger = SNew(SOceanIslandDebugger);
	TestTrue(TEXT("The debugger creates and binds a default preview texture"),
		Debugger->HasValidPreviewForTesting());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
