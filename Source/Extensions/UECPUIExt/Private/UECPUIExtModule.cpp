// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPUIExtModule.h"

#include "Tools/WidgetTools.h"
#include "Tools/WidgetTreeTools.h"
#include "Tools/WidgetLayoutTools.h"
#include "Tools/CommonUITools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPUIExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_widget_to_user_widget"),
			TEXT("edit_widget_property"),
			TEXT("edit_widget_properties"),
			TEXT("get_batch_widget_properties"),
			TEXT("add_widgets_to_layout"),
			TEXT("create_widget_from_layout"),
			TEXT("set_widget_canvas_size"),
			TEXT("get_widget_summary"),
			TEXT("set_widget_slot"),
			TEXT("set_image_brush"),
			TEXT("delete_widget"),
			TEXT("reparent_widget"),
			TEXT("list_widget_types"),
			TEXT("add_widget_animation"),
			TEXT("remove_widget_animation"),
			TEXT("get_widget_animation_summary"),
			TEXT("duplicate_widget"),
			TEXT("rename_widget"),
			TEXT("get_widget_hierarchy"),
			TEXT("set_widget_navigation"),
			TEXT("get_widget_properties"),
			TEXT("add_widget_animation_track"),
			TEXT("add_widget_animation_keyframe"),
			TEXT("set_widget_is_variable"),
			TEXT("bind_widget_property"),
			TEXT("unbind_widget_property"),
			TEXT("get_widget_bindings"),

			TEXT("get_widget_tree"),
			TEXT("get_widget_schema"),
			TEXT("apply_widget_tree"),
			TEXT("get_named_slots"),
			TEXT("set_named_slot_content"),
			TEXT("clear_named_slot_content"),
			TEXT("get_widget_layout"),
			TEXT("check_widget_overlap"),
			TEXT("get_widget_bindable_events"),

			TEXT("add_input_action_row"),
			TEXT("configure_activatable_widget"),
			TEXT("configure_common_button"),
			TEXT("create_common_button"),
			TEXT("create_common_text_block"),
			TEXT("create_widget_stack"),
			TEXT("get_common_ui_summary"),
			TEXT("set_button_input_action"),
			TEXT("set_button_style"),
			TEXT("set_common_text_style_properties"),
			TEXT("set_input_action_row_data"),
			TEXT("set_button_text_style"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPUIExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_widget_to_user_widget"),     MakeHandler(WidgetTools::HandleAddWidgetToUserWidgetFromArgs));
	D.RegisterHandler(TEXT("edit_widget_property"),          MakeHandler(WidgetTools::HandleEditWidgetPropertyFromArgs));
	D.RegisterHandler(TEXT("edit_widget_properties"),        MakeHandler(WidgetTools::HandleEditWidgetPropertyFromArgs));
	D.RegisterHandler(TEXT("get_batch_widget_properties"),   MakeHandler(WidgetTools::HandleGetBatchWidgetPropertiesFromArgs));
	D.RegisterHandler(TEXT("add_widgets_to_layout"),         MakeHandler(WidgetTools::HandleAddWidgetsToLayoutFromArgs));
	D.RegisterHandler(TEXT("create_widget_from_layout"),     MakeHandler(WidgetTools::HandleCreateWidgetFromLayoutFromArgs));
	D.RegisterHandler(TEXT("set_widget_canvas_size"),        MakeHandler(WidgetTools::HandleSetWidgetCanvasSizeFromArgs));
	D.RegisterHandler(TEXT("get_widget_summary"),            MakeHandler(WidgetTools::HandleGetWidgetSummaryFromArgs));
	D.RegisterHandler(TEXT("set_widget_slot"),               MakeHandler(WidgetTools::HandleSetWidgetSlotFromArgs));
	D.RegisterHandler(TEXT("set_image_brush"),               MakeHandler(WidgetTools::HandleSetImageBrushFromArgs));
	D.RegisterHandler(TEXT("delete_widget"),                 MakeHandler(WidgetTools::HandleDeleteWidgetFromArgs));
	D.RegisterHandler(TEXT("reparent_widget"),               MakeHandler(WidgetTools::HandleReparentWidgetFromArgs));
	D.RegisterHandler(TEXT("list_widget_types"),             MakeHandler(WidgetTools::HandleListWidgetTypesFromArgs));
	D.RegisterHandler(TEXT("add_widget_animation"),          MakeHandler(WidgetTools::HandleAddWidgetAnimationFromArgs));
	D.RegisterHandler(TEXT("remove_widget_animation"),       MakeHandler(WidgetTools::HandleRemoveWidgetAnimationFromArgs));
	D.RegisterHandler(TEXT("get_widget_animation_summary"),  MakeHandler(WidgetTools::HandleGetWidgetAnimationSummaryFromArgs));
	D.RegisterHandler(TEXT("duplicate_widget"),              MakeHandler(WidgetTools::HandleDuplicateWidgetFromArgs));
	D.RegisterHandler(TEXT("rename_widget"),                 MakeHandler(WidgetTools::HandleRenameWidgetFromArgs));
	D.RegisterHandler(TEXT("get_widget_hierarchy"),          MakeHandler(WidgetTools::HandleGetWidgetHierarchyFromArgs));
	D.RegisterHandler(TEXT("set_widget_navigation"),         MakeHandler(WidgetTools::HandleSetWidgetNavigationFromArgs));
	D.RegisterHandler(TEXT("get_widget_properties"),         MakeHandler(WidgetTools::HandleGetWidgetPropertiesFromArgs));
	D.RegisterHandler(TEXT("add_widget_animation_track"),    MakeHandler(WidgetTools::HandleAddWidgetAnimationTrackFromArgs));
	D.RegisterHandler(TEXT("add_widget_animation_keyframe"), MakeHandler(WidgetTools::HandleAddWidgetAnimationKeyframeFromArgs));
	D.RegisterHandler(TEXT("set_widget_is_variable"),        MakeHandler(WidgetTools::HandleSetWidgetIsVariableFromArgs));
	D.RegisterHandler(TEXT("bind_widget_property"),          MakeHandler(WidgetTools::HandleBindWidgetPropertyFromArgs));
	D.RegisterHandler(TEXT("unbind_widget_property"),        MakeHandler(WidgetTools::HandleUnbindWidgetPropertyFromArgs));
	D.RegisterHandler(TEXT("get_widget_bindings"),           MakeHandler(WidgetTools::HandleGetWidgetBindingsFromArgs));

	D.RegisterHandler(TEXT("get_widget_tree"),               MakeHandler(WidgetTreeTools::HandleGetWidgetTreeFromArgs));
	D.RegisterHandler(TEXT("get_widget_schema"),             MakeHandler(WidgetTreeTools::HandleGetWidgetSchemaFromArgs));
	D.RegisterHandler(TEXT("apply_widget_tree"),             MakeHandler(WidgetTreeTools::HandleApplyWidgetTreeFromArgs));
	D.RegisterHandler(TEXT("get_named_slots"),               MakeHandler(WidgetTreeTools::HandleGetNamedSlotsFromArgs));
	D.RegisterHandler(TEXT("set_named_slot_content"),        MakeHandler(WidgetTreeTools::HandleSetNamedSlotContentFromArgs));
	D.RegisterHandler(TEXT("clear_named_slot_content"),      MakeHandler(WidgetTreeTools::HandleClearNamedSlotContentFromArgs));
	D.RegisterHandler(TEXT("get_widget_layout"),             MakeHandler(WidgetLayoutTools::HandleGetWidgetLayoutFromArgs));
	D.RegisterHandler(TEXT("check_widget_overlap"),          MakeHandler(WidgetLayoutTools::HandleCheckWidgetOverlapFromArgs));
	D.RegisterHandler(TEXT("get_widget_bindable_events"),    MakeHandler(WidgetLayoutTools::HandleGetWidgetBindableEventsFromArgs));

	D.RegisterHandler(TEXT("add_input_action_row"),                MakeHandler(CommonUITools::HandleAddInputActionRowFromArgs));
	D.RegisterHandler(TEXT("configure_activatable_widget"),        MakeHandler(CommonUITools::HandleConfigureActivatableWidgetFromArgs));
	D.RegisterHandler(TEXT("configure_common_button"),             MakeHandler(CommonUITools::HandleConfigureCommonButtonFromArgs));
	D.RegisterHandler(TEXT("create_common_button"),                MakeHandler(CommonUITools::HandleCreateCommonButtonFromArgs));
	D.RegisterHandler(TEXT("create_common_text_block"),            MakeHandler(CommonUITools::HandleCreateCommonTextBlockFromArgs));
	D.RegisterHandler(TEXT("create_widget_stack"),                 MakeHandler(CommonUITools::HandleCreateWidgetStackFromArgs));
	D.RegisterHandler(TEXT("get_common_ui_summary"),               MakeHandler(CommonUITools::HandleGetCommonUISummaryFromArgs));
	D.RegisterHandler(TEXT("set_button_input_action"),             MakeHandler(CommonUITools::HandleSetButtonInputActionFromArgs));
	D.RegisterHandler(TEXT("set_button_style"),                    MakeHandler(CommonUITools::HandleSetButtonStyleFromArgs));
	D.RegisterHandler(TEXT("set_common_text_style_properties"),    MakeHandler(CommonUITools::HandleSetCommonTextStylePropertiesFromArgs));
	D.RegisterHandler(TEXT("set_input_action_row_data"),           MakeHandler(CommonUITools::HandleSetInputActionRowDataFromArgs));
	D.RegisterHandler(TEXT("set_button_text_style"),               MakeHandler(CommonUITools::HandleSetButtonTextStyleFromArgs));

	{
		const FName U(TEXT("widget"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_widget_to_user_widget"),  TEXT("Add a widget into a UserWidget's tree under a parent."), TEXT("widget_path, widget_type, widget_name, parent_name?"));
		Meta(TEXT("edit_widget_property"),        TEXT("Set a widget property (shorthand names normalised; JSON arrays for vec2). Alias edit_widget_properties (batch)."), TEXT("widget_path, widget_name, property_name, value"));
		Meta(TEXT("edit_widget_properties"),      TEXT("Alias of edit_widget_property (batch)."), TEXT("widget_path, widget_name, properties"));
		Meta(TEXT("get_batch_widget_properties"), TEXT("Read properties across many widgets in one call."), TEXT("widget_path, widgets"));
		Meta(TEXT("add_widgets_to_layout"),       TEXT("Add multiple widgets into a layout in one call."), TEXT("widget_path, widgets"));
		Meta(TEXT("create_widget_from_layout"),   TEXT("Build a UserWidget tree from a declarative layout spec."), TEXT("widget_path, layout"));
		Meta(TEXT("set_widget_canvas_size"),      TEXT("Set the design-time canvas/screen size."), TEXT("widget_path, width, height"));
		Meta(TEXT("get_widget_summary"),          TEXT("Summarise a UserWidget (tree + key properties)."), TEXT("widget_path"));
		Meta(TEXT("set_widget_slot"),             TEXT("Set a widget's slot properties (anchors/offsets/alignment; JSON arrays for vec2)."), TEXT("widget_path, widget_name, <slot props>"));
		Meta(TEXT("set_image_brush"),             TEXT("Set an Image widget's brush (texture/material/tint/size)."), TEXT("widget_path, widget_name, texture_path|material_path, tint?, size?"));
		Meta(TEXT("delete_widget"),               TEXT("Delete a widget from the tree."), TEXT("widget_path, widget_name"));
		Meta(TEXT("reparent_widget"),             TEXT("Move a widget under a new parent."), TEXT("widget_path, widget_name, new_parent"));
		Meta(TEXT("list_widget_types"),           TEXT("List available widget types."), TEXT("filter?"));
		Meta(TEXT("duplicate_widget"),            TEXT("Duplicate a widget in the tree."), TEXT("widget_path, widget_name, new_name"));
		Meta(TEXT("rename_widget"),               TEXT("Rename a widget."), TEXT("widget_path, old_name, new_name"));
		Meta(TEXT("get_widget_hierarchy"),        TEXT("Get the widget tree hierarchy."), TEXT("widget_path"));
		Meta(TEXT("get_widget_properties"),       TEXT("Read a widget's properties."), TEXT("widget_path, widget_name"));
		Meta(TEXT("set_widget_navigation"),       TEXT("Set a widget's navigation rules (up/down/left/right)."), TEXT("widget_path, widget_name, <directions>"));
		Meta(TEXT("set_widget_is_variable"),      TEXT("Toggle a widget's IsVariable (expose it as a Blueprint variable)."), TEXT("widget_path, widget_name, is_variable"));
		Meta(TEXT("add_widget_animation"),        TEXT("Add a widget animation."), TEXT("widget_path, animation_name"));
		Meta(TEXT("remove_widget_animation"),     TEXT("Remove a widget animation."), TEXT("widget_path, animation_name"));
		Meta(TEXT("get_widget_animation_summary"),TEXT("Summarise a UserWidget's animations + tracks."), TEXT("widget_path"));
		Meta(TEXT("add_widget_animation_track"),  TEXT("Add a track (targeting a widget property) to a widget animation."), TEXT("widget_path, animation_name, widget_name, property_name"));
		Meta(TEXT("add_widget_animation_keyframe"), TEXT("Add a keyframe to a widget animation track."), TEXT("widget_path, animation_name, widget_name, property_name, time, value"));
		Meta(TEXT("bind_widget_property"),        TEXT("Bind a widget property to a function/property binding."), TEXT("widget_path, widget_name, property_name, source"));
		Meta(TEXT("unbind_widget_property"),      TEXT("Remove a widget property binding."), TEXT("widget_path, widget_name, property_name"));
		Meta(TEXT("get_widget_bindings"),         TEXT("List a UserWidget's property bindings."), TEXT("widget_path"));
		Meta(TEXT("get_widget_tree"),             TEXT("Get the full widget tree structure."), TEXT("widget_path"));
		Meta(TEXT("get_widget_schema"),           TEXT("Get the settable-property schema for widget types."), TEXT("widget_type?"));
		Meta(TEXT("apply_widget_tree"),           TEXT("Apply a declarative widget-tree spec (rebuild the tree)."), TEXT("widget_path, tree"));
		Meta(TEXT("get_named_slots"),             TEXT("List a UserWidget's named slots."), TEXT("widget_path"));
		Meta(TEXT("set_named_slot_content"),      TEXT("Set a named slot's content widget."), TEXT("widget_path, slot_name, content"));
		Meta(TEXT("clear_named_slot_content"),    TEXT("Clear a named slot's content."), TEXT("widget_path, slot_name"));
		Meta(TEXT("get_widget_layout"),           TEXT("Get computed layout geometry of widgets."), TEXT("widget_path"));
		Meta(TEXT("check_widget_overlap"),        TEXT("Detect overlapping widgets in the layout."), TEXT("widget_path"));
		Meta(TEXT("get_widget_bindable_events"),  TEXT("List a widget's bindable events (OnClicked, etc.)."), TEXT("widget_path, widget_name"));
	}

	{
		const FName U(TEXT("common_ui"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_common_button"),        TEXT("Create a CommonButton widget."), TEXT("widget_path, widget_name, parent_name?"));
		Meta(TEXT("create_common_text_block"),    TEXT("Create a CommonTextBlock widget."), TEXT("widget_path, widget_name, text?, parent_name?"));
		Meta(TEXT("create_widget_stack"),         TEXT("Create a CommonActivatableWidgetStack."), TEXT("widget_path, widget_name, parent_name?"));
		Meta(TEXT("configure_common_button"),     TEXT("Configure a CommonButton (style, text, input action)."), TEXT("widget_path, widget_name, <options>"));
		Meta(TEXT("configure_activatable_widget"),TEXT("Configure a CommonActivatableWidget (auto-activate, back handler, input config)."), TEXT("widget_path, <options>"));
		Meta(TEXT("set_button_style"),            TEXT("Set a CommonButton's style asset (CommonButtonStyle)."), TEXT("widget_path, widget_name, style_path"));
		Meta(TEXT("set_button_text_style"),       TEXT("Set the text style on a CommonButtonStyle (text styles live on the button style, not the button)."), TEXT("style_path, text_style_path"));
		Meta(TEXT("set_button_input_action"),     TEXT("Set a CommonButton's triggering input action."), TEXT("widget_path, widget_name, input_action"));
		Meta(TEXT("set_common_text_style_properties"), TEXT("Set a CommonTextStyle asset's font/color/etc."), TEXT("style_path, <properties>"));
		Meta(TEXT("add_input_action_row"),        TEXT("Add a row to a CommonUI InputAction DataTable."), TEXT("table_path, row_name, <data>"));
		Meta(TEXT("set_input_action_row_data"),   TEXT("Set an InputAction DataTable row's data."), TEXT("table_path, row_name, <data>"));
		Meta(TEXT("get_common_ui_summary"),       TEXT("Summarise the CommonUI widgets/styles in a Blueprint."), TEXT("widget_path"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("UI"));
		Reg.RegisterType(TEXT("WidgetBlueprint"),      UECPCreateAsset::FactoryFromArgsFn(&WidgetTools::HandleCreateWidgetBlueprintFromArgs,        TEXT("WidgetBlueprint")),      ExtId);
		Reg.RegisterType(TEXT("ActivatableWidget"),    UECPCreateAsset::FactoryFromArgsFn(&CommonUITools::HandleCreateActivatableWidgetFromArgs,    TEXT("ActivatableWidget")),    ExtId);
		Reg.RegisterType(TEXT("CommonButtonStyle"),    UECPCreateAsset::FactoryFromArgsFn(&CommonUITools::HandleCreateCommonButtonStyleFromArgs,    TEXT("CommonButtonStyle")),    ExtId);
		Reg.RegisterType(TEXT("CommonTextStyle"),      UECPCreateAsset::FactoryFromArgsFn(&CommonUITools::HandleCreateCommonTextStyleFromArgs,      TEXT("CommonTextStyle")),      ExtId);
		Reg.RegisterType(TEXT("InputActionDataTable"), UECPCreateAsset::FactoryFromArgsFn(&CommonUITools::HandleCreateInputActionDataTableFromArgs, TEXT("InputActionDataTable")), ExtId);
	}

	UE_LOG(LogUECPUIExt, Log, TEXT("Registered %d UI tools (widget + widget_tree + widget_layout + common_ui umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPUIExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("WidgetBlueprint"), TEXT("ActivatableWidget"),
	                        TEXT("CommonButtonStyle"), TEXT("CommonTextStyle"),
	                        TEXT("InputActionDataTable") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPUIExtModule, UECPUIExt)
