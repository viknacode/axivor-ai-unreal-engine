// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/GraphTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "Editor/UMGEditor/Public/WidgetBlueprintEditor.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/MaterialNodeDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "BehaviorTreeEditor.h"
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Editor.h"

namespace GraphTools
{

void HandleGetSelectedNodes(FString& OutNodesText, FString& OutError)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		OutError = "Could not get the Asset Editor Subsystem.";
		return;
	}

	IAssetEditorInstance* ActiveEditor = nullptr;
	double LastActivationTime = 0.0;
	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* Asset : EditedAssets)
	{
		TArray<IAssetEditorInstance*> Editors = AssetEditorSubsystem->FindEditorsForAsset(Asset);
		for (IAssetEditorInstance* Editor : Editors)
		{
			if (Editor && Editor->GetLastActivationTime() > LastActivationTime)
			{
				LastActivationTime = Editor->GetLastActivationTime();
				ActiveEditor = Editor;
			}
		}
	}

	if (!ActiveEditor)
	{
		OutError = "No active asset editor found.";
		return;
	}

	const FName EditorName = ActiveEditor->GetEditorName();
	TSet<UObject*> SelectedNodes;
	bool bFoundEditor = false;

	if (EditorName == FName(TEXT("BlueprintEditor")))
	{
		FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor);
		SelectedNodes = BlueprintEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBpGraphDescriber Describer;
		OutNodesText = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("WidgetBlueprintEditor")))
	{
		FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(ActiveEditor);
		SelectedNodes = WidgetEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBpGraphDescriber Describer;
		OutNodesText = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("AnimationBlueprintEditor")))
	{
		FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor);
		SelectedNodes = BlueprintEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBpGraphDescriber Describer;
		OutNodesText = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("MaterialEditor")))
	{
		FMaterialEditor* MaterialEditor = static_cast<FMaterialEditor*>(ActiveEditor);
		SelectedNodes = MaterialEditor->GetSelectedNodes();
		bFoundEditor = true;
		FMaterialNodeDescriber Describer;
		OutNodesText = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("Behavior Tree")))
	{
		FBehaviorTreeEditor* BehaviorTreeEditor = static_cast<FBehaviorTreeEditor*>(ActiveEditor);
		SelectedNodes = BehaviorTreeEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBtGraphDescriber Describer;
		OutNodesText = Describer.DescribeSelection(SelectedNodes);
	}

	if (!bFoundEditor)
	{
		OutError = FString::Printf(TEXT("The active editor ('%s') is not a supported editor type for node selection."), *EditorName.ToString());
		return;
	}

	if (SelectedNodes.Num() == 0)
	{
		OutNodesText = "No nodes are currently selected in the active graph editor.";
		return;
	}
}

}
