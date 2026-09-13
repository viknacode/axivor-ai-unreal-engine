// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPEditorUIExtModule.h"
#include "Tools/EditorUITools.h"
#include "EditorUIRefRegistry.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPEditorUIExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("editor_ui_snapshot"),
			TEXT("editor_ui_observe"),
			TEXT("editor_ui_unobserve"),
			TEXT("editor_ui_list_observers"),
			TEXT("editor_ui_screenshot"),
			TEXT("editor_ui_click"),
			TEXT("editor_ui_hover"),
			TEXT("editor_ui_type"),
			TEXT("editor_ui_press_key"),
			TEXT("editor_ui_select_option"),
			TEXT("editor_ui_drag"),
			TEXT("editor_ui_windows"),
			TEXT("editor_ui_wait_for"),
			TEXT("editor_ui_fill_form"),
		};
		return Names;
	}
}

void FUECPEditorUIExtModule::StartupModule()
{
	UE_LOG(LogUECPEditorUIExt, Log, TEXT("FUECPEditorUIExtModule: StartupModule"));

	if (!IUECPCoreModule::IsAvailable())
	{
		UE_LOG(LogUECPEditorUIExt, Warning,
			TEXT("UECPCore not available — cannot register editor_ui tools"));
		return;
	}

	FUECPEditorUIRefRegistry::Get().Initialize();

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();

	auto MakeHandler = [](TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	};

	Dispatcher.RegisterHandler(TEXT("editor_ui_snapshot"),       MakeHandler(EditorUITools::HandleSnapshotFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_observe"),        MakeHandler(EditorUITools::HandleObserveFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_unobserve"),      MakeHandler(EditorUITools::HandleUnobserveFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_list_observers"), MakeHandler(EditorUITools::HandleListObserversFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_screenshot"),     MakeHandler(EditorUITools::HandleScreenshotFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_click"),          MakeHandler(EditorUITools::HandleClickFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_hover"),          MakeHandler(EditorUITools::HandleHoverFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_type"),           MakeHandler(EditorUITools::HandleTypeFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_press_key"),      MakeHandler(EditorUITools::HandlePressKeyFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_select_option"),  MakeHandler(EditorUITools::HandleSelectOptionFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_drag"),           MakeHandler(EditorUITools::HandleDragFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_windows"),        MakeHandler(EditorUITools::HandleWindowsFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_wait_for"),       MakeHandler(EditorUITools::HandleWaitForFromArgs));
	Dispatcher.RegisterHandler(TEXT("editor_ui_fill_form"),      MakeHandler(EditorUITools::HandleFillFormFromArgs));

	{
		const FName U(TEXT("editor_ui"));
		const auto Meta = [&Dispatcher, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ Dispatcher.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("editor_ui_snapshot"),       TEXT("Walk the live Slate widget tree from a ref (empty = all windows) → indented text snapshot with stable refs."), TEXT("ref?, max_depth?, include_source_locations?"));
		Meta(TEXT("editor_ui_observe"),        TEXT("Register an observer over a subtree so newly-appearing widgets get refs (~100ms walk)."), TEXT("ref?, max_depth?"));
		Meta(TEXT("editor_ui_unobserve"),      TEXT("Remove an observer."), TEXT("observer_id"));
		Meta(TEXT("editor_ui_list_observers"), TEXT("List active observers (root ref, depth, last walked count)."), TEXT(""));
		Meta(TEXT("editor_ui_screenshot"),     TEXT("Render a widget subtree to a base64 PNG data URI (empty ref = active window)."), TEXT("ref?"));
		Meta(TEXT("editor_ui_click"),          TEXT("Click a widget at its center."), TEXT("ref, button?=left|right|middle, double?, modifiers?"));
		Meta(TEXT("editor_ui_hover"),          TEXT("Hover the mouse over a widget (triggers hover state / tooltips)."), TEXT("ref"));
		Meta(TEXT("editor_ui_type"),           TEXT("Focus a text-input widget and type text — target the inner SEditableText/SEditableTextBox, NOT a search-box container."), TEXT("ref, text, submit?"));
		Meta(TEXT("editor_ui_press_key"),      TEXT("Press+release a key on the focused widget (Ctrl/Shift/Alt/Cmd prefixes, e.g. \"Ctrl+S\")."), TEXT("key"));
		Meta(TEXT("editor_ui_select_option"),  TEXT("Open a combobox and click the option whose text matches value (case-sensitive)."), TEXT("ref, value"));
		Meta(TEXT("editor_ui_drag"),           TEXT("Drag from start_ref to end_ref via 8 interpolated steps."), TEXT("start_ref, end_ref, modifiers?"));
		Meta(TEXT("editor_ui_windows"),        TEXT("List / focus / close top-level editor windows."), TEXT("mode=list|select|close, index?"));
		Meta(TEXT("editor_ui_wait_for"),       TEXT("Single-shot check that text is present and/or absent across observed widgets."), TEXT("text?, text_gone?"));
		Meta(TEXT("editor_ui_fill_form"),      TEXT("Apply a batch of textbox/checkbox/combobox edits in one call."), TEXT("fields=[{ref, value, field_type=textbox|checkbox|combobox}]"));
	}

	UE_LOG(LogUECPEditorUIExt, Log,
		TEXT("Registered %d editor_ui tools with the dispatcher"), OwnedToolNames().Num());
}

void FUECPEditorUIExtModule::ShutdownModule()
{
	UE_LOG(LogUECPEditorUIExt, Log, TEXT("FUECPEditorUIExtModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		for (const FName& Name : OwnedToolNames())
		{
			Dispatcher.UnregisterHandler(Name);
		}
	}

	FUECPEditorUIRefRegistry::Get().Shutdown();
}

IMPLEMENT_MODULE(FUECPEditorUIExtModule, UECPEditorUIExt)
