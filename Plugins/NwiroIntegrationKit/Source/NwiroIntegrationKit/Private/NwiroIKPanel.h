// Copyright 2026 Nwiro. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Docking/SDockTab.h"

class SWebBrowser;

#if PLATFORM_WINDOWS
class FNwiroIKImeBridge;
#endif

/**
 * Editor panel for Nwiro — Embedded React frontend via WebBrowser widget.
 */
class FNwiroIKPanel : public TSharedFromThis<FNwiroIKPanel>
{
public:
	static TSharedPtr<FNwiroIKPanel> Instance;
	static TSharedPtr<SWebBrowser> WebBrowserWidget;

	static void Initialize();
	static void Shutdown();
	static void OpenPanel();

	/** Add a log entry (shown in UE log) */
	static void AddLog(const FString& Message);

private:
	TSharedPtr<SDockTab> PanelDock;

#if PLATFORM_WINDOWS
	// IME message-forwarding bridge — Windows-only. Lifetime tied to the panel
	// instance; registered with FWindowsApplication on tab spawn and removed
	// on tab close or module shutdown. See NwiroIKImeBridge.h.
	TSharedPtr<FNwiroIKImeBridge> ImeBridge;
#endif

	void RegisterMenus();
	static void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs);
	void CreateBrowserWidget();
};
