// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"
#include "Services/IUECPNotificationService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"

bool SUECPMainWidget::TryDispatchBlueprintTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (TryDispatchTemplatesTool(ToolName, Arguments, OutResult)) return true;
	return false;
}

void SUECPMainWidget::ArrangeActiveGraph()
{
	bool bSelectedOnly = false;
	bool bGeidOnly = false;
	if (UAssetEditorSubsystem* AES = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
	{
		IAssetEditorInstance* ActiveEditor = nullptr;
		double LastTime = 0.0;
		for (UObject* Asset : AES->GetAllEditedAssets())
			for (IAssetEditorInstance* Ed : AES->FindEditorsForAsset(Asset))
				if (Ed && Ed->GetLastActivationTime() > LastTime)
				{ LastTime = Ed->GetLastActivationTime(); ActiveEditor = Ed; }

		if (ActiveEditor)
		{
			const FName EdName = ActiveEditor->GetEditorName();
			if (EdName == TEXT("BlueprintEditor") || EdName == TEXT("AnimationBlueprintEditor"))
			{
				FBlueprintEditor* BPEd = static_cast<FBlueprintEditor*>(ActiveEditor);
				int32 Sel = 0;
				for (UObject* O : BPEd->GetSelectedNodes())
					if (Cast<UEdGraphNode>(O)) Sel++;

				if (Sel >= 2)
				{
					bSelectedOnly = true;
				}
				else if (UEdGraph* FocusedGraph = BPEd->GetFocusedGraph())
				{
					for (UEdGraphNode* Node : FocusedGraph->Nodes)
					{
						if (!Node) continue;
						bool bHasId = Node->NodeComment.Contains(TEXT("GEID:"));
#if WITH_METADATA
						if (!bHasId)
						{
							if (UPackage* Pkg = Node->GetPackage())
								bHasId = !Pkg->GetMetaData().GetValue(Node, TEXT("UECP.LogicalId")).IsEmpty();
						}
#endif
						if (bHasId) { bGeidOnly = true; break; }
					}
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	if (bSelectedOnly) Args->SetBoolField(TEXT("selected_only"), true);
	if (bGeidOnly)     Args->SetBoolField(TEXT("geid_only"), true);

	if (!IUECPCoreModule::IsAvailable()) return;
	const FUECPToolResult R = IUECPCoreModule::Get().GetToolDispatcher()
		.ExecuteFromArgs(TEXT("arrange_blueprint_nodes"), Args);
	if (!R.ErrorMessage.IsEmpty()) return;

	int32 MovedCount = 0;
	TSharedPtr<FJsonObject> ResultJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ResultJson);
	if (FJsonSerializer::Deserialize(Reader, ResultJson) && ResultJson.IsValid())
	{
		double Moved = 0;
		ResultJson->TryGetNumberField(TEXT("nodes_moved"), Moved);
		MovedCount = (int32)Moved;
	}

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().GetNotificationService().PushToast(
			FString::Printf(TEXT("Arranged %d nodes%s"), MovedCount, bSelectedOnly ? TEXT(" (selected)") : TEXT("")),
			TEXT("success"));
	}
}
