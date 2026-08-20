// Copyright Epic Games, Inc. All Rights Reserved.

#include "OceanCoreEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SOceanIslandDebugger.h"

#define LOCTEXT_NAMESPACE "FOceanCoreEditorModule"

namespace OceanCoreEditor
{
	const FName IslandDebuggerTabName(TEXT("OceanCoreIslandDebugger"));
}

void FOceanCoreEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		OceanCoreEditor::IslandDebuggerTabName,
		FOnSpawnTab::CreateRaw(this, &FOceanCoreEditorModule::SpawnIslandDebuggerTab))
		.SetDisplayName(LOCTEXT("IslandDebuggerTabTitle", "Ocean Core Island Debugger"))
		.SetTooltipText(LOCTEXT("IslandDebuggerTabTooltip", "Preview Ocean Core island generation settings."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	ToolMenusStartupHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOceanCoreEditorModule::RegisterMenus));
}

void FOceanCoreEditorModule::ShutdownModule()
{
	if (UObjectInitialized() && ToolMenusStartupHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(ToolMenusStartupHandle);
		UToolMenus::UnregisterOwner(this);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OceanCoreEditor::IslandDebuggerTabName);
}

void FOceanCoreEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Tools");
	Section.AddMenuEntry(
		"OpenOceanCoreIslandDebugger",
		LOCTEXT("OpenIslandDebugger", "Ocean Core Island Debugger"),
		LOCTEXT("OpenIslandDebuggerTooltip", "Open the Ocean Core generation settings debugger."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility"),
		FUIAction(FExecuteAction::CreateLambda([]
			{
				FGlobalTabmanager::Get()->TryInvokeTab(OceanCoreEditor::IslandDebuggerTabName);
			})));
}

TSharedRef<SDockTab> FOceanCoreEditorModule::SpawnIslandDebuggerTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SOceanIslandDebugger)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOceanCoreEditorModule, OceanCoreEditor)
