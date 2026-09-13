// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace WidgetTools
{
	UECPUIEXT_API void HandleAddWidgetToUserWidget(const FString& WidgetPath, const FString& WidgetType, const FString& WidgetName, const FString& ParentName, FString& OutNewName, FString& OutError);

	UECPUIEXT_API void HandleCreateWidgetFromLayout(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Layout, FString& OutError);

	UECPUIEXT_API void HandleAddWidgetsToLayout(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Layout, FString& OutError);

	UECPUIEXT_API void HandleEditWidgetProperties(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutError);

	UECPUIEXT_API void HandleGetBatchWidgetProperties(const TArray<FString>& WidgetClasses, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleEditWidgetProperty(const FString& WidgetPath, const FString& WidgetName, const FString& PropertyName, const FString& Value, FString& OutError, FString* OutApplied = nullptr);

	UECPUIEXT_API void HandleDeleteWidget(const FString& WidgetPath, const FString& WidgetName, FString& OutError);

	UECPUIEXT_API void HandleSetWidgetCanvasSize(const FString& WidgetPath, const FString& CanvasSize, const FString& DesignMode, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleGetWidgetSummary(const FString& WidgetPath, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleSetWidgetSlot(const FString& WidgetPath, const FString& WidgetName, const TSharedRef<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleSetImageBrush(const FString& WidgetPath, const FString& WidgetName, const FString& TexturePath, const FString& ImageSize, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleReparentWidget(const FString& WidgetPath, const FString& WidgetName, const FString& NewParentName, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleListWidgetTypes(const FString& Filter, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleAddWidgetAnimation(const FString& WidgetPath, const FString& AnimationName, float Duration, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleRemoveWidgetAnimation(const FString& WidgetPath, const FString& AnimationName, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleGetWidgetAnimationSummary(const FString& WidgetPath, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleDuplicateWidget(const FString& WidgetPath, const FString& WidgetName, const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleGetWidgetHierarchy(const FString& WidgetPath, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleSetWidgetNavigation(const FString& WidgetPath, const FString& WidgetName, const FString& Direction, const FString& TargetWidgetName, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleGetWidgetProperties(const FString& WidgetPath, const FString& WidgetName, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleAddWidgetAnimationTrack(const FString& WidgetPath, const FString& AnimationName,
		const FString& WidgetName, const FString& PropertyName,
		FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleAddWidgetAnimationKeyframe(const FString& WidgetPath, const FString& AnimationName,
		const FString& WidgetName, const FString& PropertyName,
		float Time, float Value, const FString& InterpMode,
		FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleEditWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleCreateWidgetBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleAddWidgetToUserWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleGetBatchWidgetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleAddWidgetsToLayoutFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleCreateWidgetFromLayoutFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleSetWidgetCanvasSizeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleGetWidgetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleSetWidgetSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleSetImageBrushFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleDeleteWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleReparentWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleListWidgetTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleAddWidgetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleRemoveWidgetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleGetWidgetAnimationSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleDuplicateWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleRenameWidgetFromArgs   (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleGetWidgetHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleSetWidgetNavigationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleGetWidgetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleAddWidgetAnimationTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPUIEXT_API void HandleAddWidgetAnimationKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	FString JsonValueToPropertyString(const TSharedPtr<FJsonValue>& Val);

	FString NormalizeWidgetPropertyName(const FString& Key);

	UECPUIEXT_API void ExtractInlineProperties(const TSharedPtr<FJsonObject>& WidgetDef, TArray<TPair<FString, FString>>& OutProperties);

	UECPUIEXT_API void HandleSetWidgetIsVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleBindWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleUnbindWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPUIEXT_API void HandleGetWidgetBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
