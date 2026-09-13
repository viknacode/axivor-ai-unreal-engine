// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Utils/EditorRuntime.h"
#include "UECPCoreModule.h"
#include "Services/IUECPLicenseService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPExtensionService.h"
#include "Services/UECPToolSafety.h"
#include "LearningManager.h"
#include "UIConfigManager.h"
#include "Utils/WidgetUtils.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Misc/Base64.h"
#include "Containers/Ticker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Describers/BpGraphDescriber.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node_Event.h"
#include "EdGraphSchema_K2.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "Interfaces/IPluginManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Internationalization/Regex.h"
#include "WebBrowserModule.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Describers/MaterialNodeDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "Materials/Material.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "UObject/Interface.h"
#include "Kismet2/StructureEditorUtils.h"
#include "IMaterialEditor.h"
#include "UECPShellModule.h"
#include "BehaviorTreeEditor.h"
#include "Editor/UMGEditor/Public/WidgetBlueprintEditor.h"
#include "MaterialGraph/MaterialGraphNode.h"
#if WITH_EDITOR
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/Texture.h"
#endif
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Components/ContentWidget.h"
#include "UObject/TextProperty.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "SUECPAssetMentionPopup.h"
#include "AssetReferenceManager.h"
#include "ApiKeyManager.h"
#include "TextureGenManager.h"
#include "MeshAssetManager.h"
#include "Widgets/Images/SImage.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SOverlay.h"
#include "Async/Async.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Tools/ProjectScanTools.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "AssetToolsModule.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Framework/Text/TextLayout.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

static FString RepairJson(const FString& In)
{
	auto IsValidJsonEscape = [](TCHAR c) -> bool
	{
		return c == '"' || c == '\\' || c == '/' ||
		       c == 'b' || c == 'f'  || c == 'n'  ||
		       c == 'r' || c == 't'  || c == 'u';
	};

	FString Pass1;
	Pass1.Reserve(In.Len() + 64);
	bool bInString = false;
	bool bEscaped  = false;

	for (int32 i = 0; i < In.Len(); ++i)
	{
		TCHAR Ch = In[i];

		if (bEscaped)
		{
			Pass1 += Ch;
			bEscaped = false;
			continue;
		}

		if (Ch == '\\' && bInString)
		{
			TCHAR Next = (i + 1 < In.Len()) ? In[i + 1] : 0;
			if (IsValidJsonEscape(Next))
			{
				Pass1 += Ch;
				bEscaped = true;
			}
			else
			{
				Pass1 += TEXT("\\\\");
			}
			continue;
		}

		if (Ch == '"')
		{
			bInString = !bInString;
			Pass1 += Ch;
			continue;
		}

		if (bInString)
		{
			if      (Ch == '\n') { Pass1 += TEXT("\\n");  continue; }
			else if (Ch == '\r') { Pass1 += TEXT("\\r");  continue; }
			else if (Ch == '\t') { Pass1 += TEXT("\\t");  continue; }
		}

		Pass1 += Ch;
	}

	FString Pass1_5;
	Pass1_5.Reserve(Pass1.Len());
	{
		bool bInStr_15 = false;
		bool bEsc_15 = false;
		for (int32 i = 0; i < Pass1.Len(); ++i)
		{
			TCHAR Ch = Pass1[i];
			if (bEsc_15)       { Pass1_5 += Ch; bEsc_15 = false; continue; }
			if (bInStr_15 && Ch == TEXT('\\')) { Pass1_5 += Ch; bEsc_15 = true; continue; }
			if (Ch == TEXT('"'))
			{
				Pass1_5 += Ch;
				if (bInStr_15)
				{
					int32 j = i + 1;
					while (j < Pass1.Len() && (Pass1[j] == ' ' || Pass1[j] == '\t'))
					{
						Pass1_5 += Pass1[j];
						++j;
					}
					if (j < Pass1.Len() && Pass1[j] == TEXT('='))
					{
						Pass1_5 += TEXT(':');
						i = j;
					}
				}
				bInStr_15 = !bInStr_15;
				continue;
			}
			Pass1_5 += Ch;
		}
	}

	FString Pass2;
	Pass2.Reserve(Pass1_5.Len());
	for (int32 i = 0; i < Pass1_5.Len(); ++i)
	{
		if (Pass1_5[i] == ',')
		{
			int32 j = i + 1;
			while (j < Pass1_5.Len() && (Pass1_5[j] == ' ' || Pass1_5[j] == '\t' || Pass1_5[j] == '\n' || Pass1_5[j] == '\r'))
				++j;
			if (j < Pass1_5.Len() && (Pass1_5[j] == '}' || Pass1_5[j] == ']'))
				continue;
		}
		Pass2 += Pass1_5[i];
	}

	FString Out;
	Out.Reserve(Pass2.Len());
	int32 ObjDepth = 0;
	int32 ArrDepth = 0;
	int32 DroppedBraces = 0;
	int32 DroppedBrackets = 0;
	bool bInStr3 = false;
	bool bEsc3 = false;
	for (int32 i = 0; i < Pass2.Len(); ++i)
	{
		TCHAR Ch = Pass2[i];
		if (bEsc3)       { Out += Ch; bEsc3 = false; continue; }
		if (bInStr3 && Ch == TEXT('\\')) { Out += Ch; bEsc3 = true; continue; }
		if (Ch == TEXT('"')) { bInStr3 = !bInStr3; Out += Ch; continue; }
		if (bInStr3)     { Out += Ch; continue; }
		if (Ch == TEXT('{')) { ObjDepth++; Out += Ch; continue; }
		if (Ch == TEXT('[')) { ArrDepth++; Out += Ch; continue; }
		if (Ch == TEXT('}'))
		{
			if (ObjDepth > 0) { ObjDepth--; Out += Ch; }
			else { ++DroppedBraces; }
			continue;
		}
		if (Ch == TEXT(']'))
		{
			if (ArrDepth > 0) { ArrDepth--; Out += Ch; }
			else { ++DroppedBrackets; }
			continue;
		}
		Out += Ch;
	}

	if (DroppedBraces > 0 || DroppedBrackets > 0)
	{
		UE_LOG(LogUECPShell, Warning,
			TEXT("[JSON repair] Dropped %d excess '}' and %d excess ']' — AI batch may have been truncated. Original length %d, repaired %d."),
			DroppedBraces, DroppedBrackets, Pass2.Len(), Out.Len());
	}

	return Out;
}

bool SUECPMainWidget::TryParseToolCallJson(const FString& JsonString, FToolCallExtractionResult& OutResult)
{

	auto TryDeserialize = [](const FString& Src, TSharedPtr<FJsonObject>& Out) -> bool
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Src);
		return FJsonSerializer::Deserialize(R, Out) && Out.IsValid();
	};

	TSharedPtr<FJsonObject> JsonObject;
	if (!TryDeserialize(JsonString, JsonObject))
	{
		FString Repaired = RepairJson(JsonString);
		if (!TryDeserialize(Repaired, JsonObject))
		{
			return false;
		}
	}

	FString ToolName;
	if (!JsonObject->TryGetStringField(TEXT("tool_name"), ToolName))
	{
		bool bAllObjects = JsonObject->Values.Num() >= 1;
		for (auto& Pair : JsonObject->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			{
				bAllObjects = false;
				break;
			}
		}
		if (bAllObjects)
		{
			auto It = JsonObject->Values.CreateConstIterator();
			ToolName = It->Key;
			OutResult.bHasToolCall = true;
			OutResult.ToolName = ToolName;
			OutResult.Arguments = It->Value->AsObject();
			++It;
			FString ExtraBlocks;
			for (; It; ++It)
			{
				TSharedPtr<FJsonObject> ExtraObj = MakeShareable(new FJsonObject);
				ExtraObj->SetObjectField(FString(*It->Key), It->Value->AsObject());
				FString ExtraJson;
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ExtraJson);
				FJsonSerializer::Serialize(ExtraObj.ToSharedRef(), W);
				if (!ExtraJson.IsEmpty())
				{
					if (!ExtraBlocks.IsEmpty()) ExtraBlocks += TEXT("\n\n");
					ExtraBlocks += FString::Printf(TEXT("```json\n%s\n```"), *ExtraJson);
				}
			}
			if (!ExtraBlocks.IsEmpty())
				OutResult.ConversationalText = ExtraBlocks;
			return true;
		}
		return false;
	}

	OutResult.bHasToolCall = true;
	OutResult.ToolName = ToolName;

	const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr)
	{
		OutResult.Arguments = *ArgumentsPtr;
	}
	else
	{
		OutResult.Arguments = MakeShareable(new FJsonObject);
	}

	static const TSet<FString> ReservedTopLevelKeys = {
		TEXT("tool_name"), TEXT("arguments"), TEXT("tool_call_id"), TEXT("id"),
		TEXT("assistant_text"), TEXT("done"), TEXT("type"), TEXT("reasoning")
	};
	for (const auto& KV : JsonObject->Values)
	{
		const FString FieldName(*KV.Key);
		if (ReservedTopLevelKeys.Contains(FieldName)) continue;
		if (OutResult.Arguments->HasField(FieldName)) continue;
		OutResult.Arguments->SetField(FieldName, KV.Value);
	}

	return true;
}

FToolCallExtractionResult SUECPMainWidget::ExtractToolCallFromResponse(const FString& AiResponse)
{

	FToolCallExtractionResult Result;
	Result.ConversationalText = AiResponse;

	auto AppendTextIfPresent = [](FString& Target, const FString& Fragment)
	{
		const FString Clean = Fragment.TrimStartAndEnd();
		if (Clean.IsEmpty())
		{
			return;
		}
		if (!Target.IsEmpty())
		{
			Target += TEXT("\n");
		}
		Target += Clean;
	};

	auto SerializeJsonObject = [](const TSharedPtr<FJsonObject>& Obj) -> FString
	{
		if (!Obj.IsValid())
		{
			return FString();
		}
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	};

	auto TryExtractEnvelope = [&](const FString& JsonContent, const FString& TextBefore, const FString& TextAfter, FToolCallExtractionResult& Out) -> bool
	{
		TSharedPtr<FJsonObject> JsonObject;
		auto TryDeserialize = [](const FString& Src, TSharedPtr<FJsonObject>& Parsed) -> bool
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Src);
			return FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid();
		};

		if (!TryDeserialize(JsonContent, JsonObject))
		{
			if (!TryDeserialize(RepairJson(JsonContent), JsonObject))
			{
				return false;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
		if (!JsonObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) || !ToolCalls)
		{
			return false;
		}

		Out.bDoneFieldPresent = JsonObject->HasField(TEXT("done"));
		Out.bEnvelopeMatched = true;
		if (Out.bDoneFieldPresent)
		{
			JsonObject->TryGetBoolField(TEXT("done"), Out.bExplicitDone);
		}
		JsonObject->TryGetBoolField(TEXT("continue"), Out.bExplicitContinue);

		FString AssistantText;
		JsonObject->TryGetStringField(TEXT("assistant_text"), AssistantText);
		if (AssistantText.IsEmpty())
		{
			JsonObject->TryGetStringField(TEXT("text"), AssistantText);
		}

		FString CombinedText;
		AppendTextIfPresent(CombinedText, TextBefore);
		AppendTextIfPresent(CombinedText, AssistantText);
		AppendTextIfPresent(CombinedText, TextAfter);

		if (ToolCalls->Num() == 0)
		{
			Out.ConversationalText = CombinedText;
			return true;
		}

		const TSharedPtr<FJsonObject> FirstCall = (*ToolCalls)[0].IsValid() ? (*ToolCalls)[0]->AsObject() : nullptr;
		if (!FirstCall.IsValid())
		{
			Out.ConversationalText = CombinedText;
			return true;
		}

		FString ToolName;
		TSharedPtr<FJsonObject> Arguments = MakeShareable(new FJsonObject);
		if (FirstCall->TryGetStringField(TEXT("tool_name"), ToolName) ||
		    FirstCall->TryGetStringField(TEXT("tool"), ToolName))
		{
			const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
			if (FirstCall->TryGetObjectField(TEXT("arguments"), ArgsPtr) && ArgsPtr && ArgsPtr->IsValid())
			{
				Arguments = *ArgsPtr;
			}
			else if (FirstCall->TryGetObjectField(TEXT("parameters"), ArgsPtr) && ArgsPtr && ArgsPtr->IsValid())
			{
				Arguments = *ArgsPtr;
			}
		}
		else if (FirstCall->Values.Num() == 1)
		{
			auto It = FirstCall->Values.CreateConstIterator();
			if (It->Value.IsValid() && It->Value->Type == EJson::Object)
			{
				ToolName = It->Key;
				Arguments = It->Value->AsObject();
			}
		}
		else if (FirstCall->Values.Num() > 1)
		{
			bool bAllObjects = true;
			for (auto& Pair : FirstCall->Values)
			{
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
				{
					bAllObjects = false;
					break;
				}
			}
			if (bAllObjects)
			{
				auto It = FirstCall->Values.CreateConstIterator();
				ToolName = It->Key;
				Arguments = It->Value->AsObject();
				++It;
				FString ExtraBlocks;
				for (; It; ++It)
				{
					TSharedPtr<FJsonObject> ExtraObj = MakeShareable(new FJsonObject);
					ExtraObj->SetObjectField(FString(*It->Key), It->Value->AsObject());
					FString ExtraJson = SerializeJsonObject(ExtraObj);
					if (!ExtraJson.IsEmpty())
					{
						if (!ExtraBlocks.IsEmpty()) ExtraBlocks += TEXT("\n\n");
						ExtraBlocks += FString::Printf(TEXT("```json\n%s\n```"), *ExtraJson);
					}
				}
				if (!ExtraBlocks.IsEmpty())
				{
					if (!CombinedText.IsEmpty()) CombinedText += TEXT("\n\n");
					CombinedText += ExtraBlocks;
				}
			}
		}

		if (ToolName.IsEmpty())
		{
			Out.ConversationalText = CombinedText;
			return true;
		}

		Out.bHasToolCall = true;
		Out.ToolName = ToolName;
		Out.Arguments = Arguments;

		FString RemainingCallsText;
		for (int32 Index = 1; Index < ToolCalls->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> CallObj = (*ToolCalls)[Index].IsValid() ? (*ToolCalls)[Index]->AsObject() : nullptr;
			if (!CallObj.IsValid())
			{
				continue;
			}

			FString BlockJson = SerializeJsonObject(CallObj);
			if (BlockJson.IsEmpty())
			{
				continue;
			}

			if (!RemainingCallsText.IsEmpty())
			{
				RemainingCallsText += TEXT("\n\n");
			}
			RemainingCallsText += FString::Printf(TEXT("```json\n%s\n```"), *BlockJson);
		}

		if (!RemainingCallsText.IsEmpty())
		{
			if (!CombinedText.IsEmpty())
			{
				CombinedText += TEXT("\n\n");
			}
			CombinedText += RemainingCallsText;
		}

		Out.ConversationalText = CombinedText;
		return true;
	};

	const FString JsonBlockStart = TEXT("```json");
	const FString JsonBlockEnd = TEXT("```");

	int32 JsonBlockStartPos = AiResponse.Find(JsonBlockStart, ESearchCase::IgnoreCase);

	if (JsonBlockStartPos != INDEX_NONE)
	{
		int32 ContentStart = JsonBlockStartPos + JsonBlockStart.Len();
		int32 JsonBlockEndPos = AiResponse.Find(JsonBlockEnd, ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);

		if (JsonBlockEndPos != INDEX_NONE)
		{
			FString JsonContent = AiResponse.Mid(ContentStart, JsonBlockEndPos - ContentStart).TrimStartAndEnd();
			FString TextBefore = AiResponse.Left(JsonBlockStartPos).TrimEnd();
			FString TextAfter = AiResponse.Mid(JsonBlockEndPos + JsonBlockEnd.Len()).TrimStart();

			if (TryExtractEnvelope(JsonContent, TextBefore, TextAfter, Result))
			{
				return Result;
			}

			if (TryParseToolCallJson(JsonContent, Result))
			{
				Result.ConversationalText = TextBefore;
				if (!TextAfter.IsEmpty())
				{
					if (!Result.ConversationalText.IsEmpty())
					{
						Result.ConversationalText += TEXT("\n");
					}
					Result.ConversationalText += TextAfter;
				}
				return Result;
			}
		}
	}

	auto StripEnvelopeResidue = [](FString& Text)
	{
		if (Text.IsEmpty()) return;
		const int32 TrailDone = Text.Find(TEXT("\",\"done\""), ESearchCase::IgnoreCase);
		if (TrailDone != INDEX_NONE)
		{
			Text = Text.Left(TrailDone);
		}
		else
		{
			const int32 TrailToolCalls = Text.Find(TEXT("\",\"tool_calls\""), ESearchCase::IgnoreCase);
			if (TrailToolCalls != INDEX_NONE)
			{
				Text = Text.Left(TrailToolCalls);
			}
		}
		const FString LeadPrefix = TEXT("{\"assistant_text\":\"");
		if (Text.StartsWith(LeadPrefix, ESearchCase::IgnoreCase))
		{
			Text = Text.RightChop(LeadPrefix.Len());
		}
		Text = Text.TrimStartAndEnd();
	};

	int32 BraceStart = AiResponse.Find(TEXT("{"), ESearchCase::CaseSensitive);

	int32 AttemptCount = 0;
	while (BraceStart != INDEX_NONE)
	{
		AttemptCount++;
		int32 BraceEnd = FindMatchingClosingBrace(AiResponse, BraceStart);

		if (BraceEnd != INDEX_NONE)
		{
			FString JsonContent = AiResponse.Mid(BraceStart, BraceEnd - BraceStart + 1);
			FString TextBefore = AiResponse.Left(BraceStart).TrimEnd();
			FString TextAfter = AiResponse.Mid(BraceEnd + 1).TrimStart();

			if (TryExtractEnvelope(JsonContent, TextBefore, TextAfter, Result))
			{
				StripEnvelopeResidue(Result.ConversationalText);
				return Result;
			}

			if (TryParseToolCallJson(JsonContent, Result))
			{
				Result.ConversationalText = TextBefore;
				if (!TextAfter.IsEmpty())
				{
					if (!Result.ConversationalText.IsEmpty())
					{
						Result.ConversationalText += TEXT("\n");
					}
					Result.ConversationalText += TextAfter;
				}
				StripEnvelopeResidue(Result.ConversationalText);
				return Result;
			}
		}
		BraceStart = AiResponse.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BraceStart + 1);
	}

	int32 ToolNamePos = AiResponse.Find(TEXT("\"tool_name\""), ESearchCase::CaseSensitive);
	if (ToolNamePos != INDEX_NONE)
	{

		int32 ColonPos = AiResponse.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ToolNamePos + 11);
		if (ColonPos != INDEX_NONE)
		{
			int32 QuoteStart = AiResponse.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ColonPos + 1);
			if (QuoteStart != INDEX_NONE)
			{
				int32 QuoteEnd = AiResponse.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, QuoteStart + 1);
				if (QuoteEnd != INDEX_NONE)
				{
					FString ToolName = AiResponse.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);

					Result.bHasToolCall = true;
					Result.ToolName = ToolName;
					Result.Arguments = MakeShareable(new FJsonObject);

					int32 ArgsPos = AiResponse.Find(TEXT("\"arguments\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, QuoteEnd);
					if (ArgsPos != INDEX_NONE)
					{
						int32 ArgsColonPos = AiResponse.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ArgsPos + 10);
						if (ArgsColonPos != INDEX_NONE)
						{
							int32 ArgsBraceStart = AiResponse.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ArgsColonPos + 1);
							if (ArgsBraceStart != INDEX_NONE)
							{
								int32 ArgsBraceEnd = FindMatchingClosingBrace(AiResponse, ArgsBraceStart);
								if (ArgsBraceEnd != INDEX_NONE)
								{
									FString ArgsJson = AiResponse.Mid(ArgsBraceStart, ArgsBraceEnd - ArgsBraceStart + 1);

									TSharedPtr<FJsonObject> ArgsObject;
									auto TryArgsDeserialize = [&](const FString& Src) -> bool
									{
										TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Src);
										return FJsonSerializer::Deserialize(R, ArgsObject) && ArgsObject.IsValid();
									};
									bool bArgsParsed = TryArgsDeserialize(ArgsJson);
									if (!bArgsParsed)
									{
										bArgsParsed = TryArgsDeserialize(RepairJson(ArgsJson));
									}
									if (bArgsParsed)
									{
										Result.Arguments = ArgsObject;
									}
									else
									{

										auto ExtractStringField = [&](const FString& FieldName) -> FString
										{
											FString SearchKey = FString::Printf(TEXT("\"%s\""), *FieldName);
											int32 KeyPos = ArgsJson.Find(SearchKey, ESearchCase::CaseSensitive);
											if (KeyPos == INDEX_NONE) return FString();
											int32 ColPos = ArgsJson.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyPos + SearchKey.Len());
											if (ColPos == INDEX_NONE) return FString();
											int32 QS = ArgsJson.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ColPos + 1);
											if (QS == INDEX_NONE) return FString();
											int32 QE = ArgsJson.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, QS + 1);
											if (QE == INDEX_NONE) return FString();
											return ArgsJson.Mid(QS + 1, QE - QS - 1);
										};

										FString ActionVal = ExtractStringField(TEXT("action"));
										if (!ActionVal.IsEmpty())
										{
											Result.Arguments->SetStringField(TEXT("action"), ActionVal);
										}
										for (const FString& Field : { FString(TEXT("blueprint_path")), FString(TEXT("asset_path")), FString(TEXT("user_widget_path")), FString(TEXT("widget_path")) })
										{
											FString Val = ExtractStringField(Field);
											if (!Val.IsEmpty()) Result.Arguments->SetStringField(Field, Val);
										}
									}
								}
							}
						}
					}

					return Result;
				}
			}
		}
	}

	return Result;
}

FToolExecutionResult SUECPMainWidget::DispatchToolCall(const FString& InToolName, const TSharedPtr<FJsonObject>& Arguments)
{

	if (!IUECPCoreModule::Get().GetLicenseService().IsSessionActive())
	{
		FToolExecutionResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = EditorReadiness::GetContextDeniedMessage(0);
		return Result;
	}

	FString ToolName = InToolName;
	FString UmbrellaOrigin;
	{
		bool bIsUmbrella = UECPToolDispatch::IsUmbrellaName(FName(*ToolName));

		if (!bIsUmbrella && IUECPCoreModule::IsAvailable())
		{
			IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
			if (TOptional<FUECPExtensionDescriptor> Owner = Ext.FindExtensionByUmbrella(FName(*ToolName)); Owner.IsSet())
			{
				bIsUmbrella = (Ext.GetExtensionState(Owner->ExtensionId) == EUECPExtensionState::Loaded);
			}
		}

		if (bIsUmbrella && Arguments.IsValid())
		{
			FString Action;
			bool bFound = Arguments->TryGetStringField(TEXT("action"), Action);
			if (bFound && !Action.IsEmpty())
			{
				int32 XmlTagStart;
				if (Action.FindChar(TEXT('<'), XmlTagStart))
					Action.LeftInline(XmlTagStart, EAllowShrinking::No);
				Action.TrimStartAndEndInline();

				UmbrellaOrigin = ToolName;
				ToolName = Action;
			}
			else if (ToolName == TEXT("widget") && Arguments.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* LayoutArray = nullptr;
				FString InferredPropName;
				if (Arguments->TryGetArrayField(TEXT("layout"), LayoutArray) && LayoutArray)
					ToolName = TEXT("create_widget_from_layout");
				else if (Arguments->HasField(TEXT("canvas_size")) || Arguments->HasField(TEXT("design_mode")))
					ToolName = TEXT("set_widget_canvas_size");
				else if (Arguments->TryGetStringField(TEXT("property_name"), InferredPropName) && !InferredPropName.IsEmpty())
					ToolName = TEXT("set_widget_property");
				else if (Arguments->HasField(TEXT("anchors")) || Arguments->HasField(TEXT("position")) || Arguments->HasField(TEXT("z_order")))
					ToolName = TEXT("set_widget_slot");
				else if (Arguments->HasField(TEXT("texture_path")))
					ToolName = TEXT("set_image_brush");
				else
					ToolName = TEXT("get_widget_summary");
			}
			else
			{
				FToolExecutionResult Result;
				Result.bSuccess = false;
				Result.ErrorMessage = FString::Printf(
					TEXT("Umbrella tool '%s' requires an 'action' field. Example: %s(action='<sub_action>', ...). Call get_tool_docs(category='%s') to see valid actions."),
					*ToolName, *ToolName, *ToolName);
				return Result;
			}
		}
	}

	if (GetActiveArchitectInteractionMode() == EAIInteractionMode::JustChat)
	{
		if (!IsReadOnlyTool(ToolName))
		{
			FToolExecutionResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(
				TEXT("JUST CHAT MODE: '%s' is blocked. Only read and inspect tools (get_*, list_*, find_*, search_*) are allowed. Switch to Auto Edit to create or modify assets."),
				*ToolName
			);
			return Result;
		}
	}

	if (GetActiveArchitectInteractionMode() == EAIInteractionMode::AskBeforeEdit && !UECPToolSafety::IsPlanModeAllowed(FName(*ToolName)) && !bBypassAskBeforeEditOnce)
	{
		FString ArgsStr;
		if (Arguments.IsValid())
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsStr);
			FJsonSerializer::Serialize(Arguments.ToSharedRef(), Writer);
		}
		PendingConfirmToolName = ToolName;
		PendingConfirmArguments = Arguments;
		PendingConfirmArgsPreview = ArgsStr.Len() > 350 ? ArgsStr.Left(350) + TEXT("...") : ArgsStr;
		PendingConfirmChatID = ActiveArchitectChatID;
		bConfirmationPending = true;
		if (AppBridgeObject) AppBridgeObject->PushToolConfirmation(ToolName, PendingConfirmArgsPreview);
		ArmConfirmAutoDecide(ToolName);
		FToolExecutionResult PendingResult;
		PendingResult.bIsPending = true;
		PendingResult.bSuccess = true;
		return PendingResult;
	}
	auto TriggerConfirmGate = [&]() -> FToolExecutionResult
	{
		FString ArgsStr;
		if (Arguments.IsValid())
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsStr);
			FJsonSerializer::Serialize(Arguments.ToSharedRef(), Writer);
		}
		PendingConfirmToolName = ToolName;
		PendingConfirmArguments = Arguments;
		PendingConfirmArgsPreview = ArgsStr.Len() > 350 ? ArgsStr.Left(350) + TEXT("...") : ArgsStr;
		PendingConfirmChatID = ActiveArchitectChatID;
		bConfirmationPending = true;
		if (AppBridgeObject) AppBridgeObject->PushToolConfirmation(ToolName, PendingConfirmArgsPreview);
		ArmConfirmAutoDecide(ToolName);
		FToolExecutionResult PendingResult;
		PendingResult.bIsPending = true;
		PendingResult.bSuccess = true;
		return PendingResult;
	};

	if (!bBypassAskBeforeEditOnce
		&& GetActiveArchitectInteractionMode() == EAIInteractionMode::AutoEdit
		&& (ToolName == TEXT("run_command") || ToolName.StartsWith(TEXT("git_"))))
	{
		return TriggerConfirmGate();
	}

	bool bDestructiveConfirm = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"), bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath());
	const bool bIsDestructive = UECPToolSafety::IsDestructive(FName(*ToolName));
	if (!bBypassAskBeforeEditOnce
		&& GetActiveArchitectInteractionMode() == EAIInteractionMode::AutoEdit
		&& bDestructiveConfirm
		&& bIsDestructive)
	{
		return TriggerConfirmGate();
	}

	bBypassAskBeforeEditOnce = false;

	if (GetActiveArchitectInteractionMode() == EAIInteractionMode::PlanMode
		&& !UECPToolSafety::IsPlanModeAllowed(FName(*ToolName)))
	{
		FToolExecutionResult PlanR;
		PlanR.bSuccess = false;
		PlanR.ErrorMessage = FString::Printf(
			TEXT("PLAN MODE: '%s' is not allowed. Read, inspect, and plan tools only — switch to Auto Edit to create or modify assets."),
			*ToolName);
		return PlanR;
	}

	const FUECPToolResult Dispatched = IUECPCoreModule::Get().GetToolDispatcher()
		.ExecuteFromArgs(FName(*ToolName), Arguments);
	{
		const bool bUnreached =
			!Dispatched.bSuccess
			&& (Dispatched.ErrorMessage.StartsWith(TEXT("No handler registered"))
				|| Dispatched.ErrorMessage.StartsWith(TEXT("Unknown tool")));
		if (!bUnreached)
		{
			if (Dispatched.bSuccess
				&& (ToolName == TEXT("profile_project") || ToolName == TEXT("analyze_trace")))
			{
				LastProfileResultJson = Dispatched.ResultJson;
			}

			FToolExecutionResult Result;
			Result.bSuccess = Dispatched.bSuccess;
			Result.ResultJson = Dispatched.ResultJson;
			Result.ErrorMessage = Dispatched.ErrorMessage;
			Result.SummaryJson = Dispatched.SummaryJson;
			return Result;
		}
	}

	FToolExecutionResult R;
	if (TryDispatchBlueprintTool(ToolName, Arguments, R))         { return R; }
	if (TryDispatchAssetCoreTool(ToolName, Arguments, R))         { return R; }
	if (TryDispatchMaterialTool(ToolName, Arguments, R))          { return R; }
	if (TryDispatchAudioDepthTool(ToolName, Arguments, R))        { return R; }
	if (TryDispatchProjectVizTool(ToolName, Arguments, R))       { return R; }
	if (TryDispatchPlanTool(ToolName, Arguments, R))            { return R; }
	if (TryDispatchTaskTool(ToolName, Arguments, R))            { return R; }

	R.bSuccess = false;
	FString DispatcherHint;
	const int32 HintIdx = Dispatched.ErrorMessage.Find(TEXT(" Did you mean:"));
	if (HintIdx != INDEX_NONE) DispatcherHint = Dispatched.ErrorMessage.Mid(HintIdx);

	if (!UmbrellaOrigin.IsEmpty())
	{
		R.ErrorMessage = FString::Printf(
			TEXT("Unknown action '%s' for umbrella '%s'. Call get_tool_docs(category='%s') to see valid actions.%s (Original call: %s(action='%s', ...))"),
			*ToolName, *UmbrellaOrigin, *UmbrellaOrigin, *DispatcherHint, *UmbrellaOrigin, *ToolName);
	}
	else
	{
		R.ErrorMessage = FString::Printf(TEXT("Unknown tool: %s%s"), *ToolName, *DispatcherHint);
	}
	return R;
}

bool SUECPMainWidget::TryDispatchWidgetOwnedTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	FString Resolved = ToolName;
	FString Action;
	if (Arguments.IsValid() && Arguments->TryGetStringField(TEXT("action"), Action) && !Action.IsEmpty())
	{
		int32 XmlIdx;
		if (Action.FindChar(TEXT('<'), XmlIdx)) Action.LeftInline(XmlIdx, EAllowShrinking::No);
		Action.TrimStartAndEndInline();
		if (!Action.IsEmpty()) Resolved = Action;
	}

	if (ToolName != Resolved)
	{
		if (TryDispatchBlueprintTool(ToolName, Arguments, OutResult))  { return true; }
		if (TryDispatchAssetCoreTool(ToolName, Arguments, OutResult))  { return true; }
		if (TryDispatchMaterialTool(ToolName, Arguments, OutResult))   { return true; }
		if (TryDispatchAudioDepthTool(ToolName, Arguments, OutResult)) { return true; }
		if (TryDispatchProjectVizTool(ToolName, Arguments, OutResult)) { return true; }
		if (TryDispatchPlanTool(ToolName, Arguments, OutResult))       { return true; }
		if (TryDispatchTaskTool(ToolName, Arguments, OutResult))       { return true; }
	}

	if (TryDispatchBlueprintTool(Resolved, Arguments, OutResult))  { return true; }
	if (TryDispatchAssetCoreTool(Resolved, Arguments, OutResult))  { return true; }
	if (TryDispatchMaterialTool(Resolved, Arguments, OutResult))   { return true; }
	if (TryDispatchAudioDepthTool(Resolved, Arguments, OutResult)) { return true; }
	if (TryDispatchProjectVizTool(Resolved, Arguments, OutResult)) { return true; }
	if (TryDispatchPlanTool(Resolved, Arguments, OutResult))       { return true; }
	if (TryDispatchTaskTool(Resolved, Arguments, OutResult))       { return true; }
	return false;
}

// Axivor: a confirm prompt nobody answers must never stall the run. After the configured
// window this decides on its own — proceed in Auto Edit (the mode already grants the AI
// autonomy), skip otherwise — and the AI is told what happened so it can carry on.
void SUECPMainWidget::ArmConfirmAutoDecide(const FString& ToolName)
{
	int32 TimeoutSecs = 25;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ConfirmTimeoutSeconds"), TimeoutSecs, FSettingsManager::GetGlobalConfigPath());
	TimeoutSecs = FMath::Clamp(TimeoutSecs, 5, 600);
	bool bProceedWhenUnanswered = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ConfirmProceedWhenUnanswered"), bProceedWhenUnanswered, FSettingsManager::GetGlobalConfigPath());

	const int32  GateSerial   = ++ConfirmGateSerial;
	const bool   bAutoProceed = bProceedWhenUnanswered
		&& GetActiveArchitectInteractionMode() == EAIInteractionMode::AutoEdit;
	const FString GateTool = ToolName;
	TWeakPtr<SWidget> WeakGate = AsWeak();

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakGate, GateTool, GateSerial, bAutoProceed](float) -> bool
		{
			TSharedPtr<SWidget> Pinned = WeakGate.Pin();
			if (!Pinned.IsValid()) return false;
			SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
			// Already answered, or a newer prompt took over: nothing to resolve.
			if (!Self->bConfirmationPending || Self->ConfirmGateSerial != GateSerial) return false;
			UE_LOG(LogUECPShell, Warning, TEXT("[confirm] nobody answered '%s' — auto-%s"),
				*GateTool, bAutoProceed ? TEXT("proceeding") : TEXT("skipping"));
			if (bAutoProceed) Self->OnConfirmToolProceed();
			else              Self->OnConfirmToolSkip();
			return false; // one-shot
		}), (float)TimeoutSecs);
}

FReply SUECPMainWidget::OnConfirmToolProceed()
{
	{
		bool bClaimed = false;
		{
			FScopeLock L(&MCPConfirmTagLock);
			if (!MCPPendingConfirmTool.IsEmpty())
			{
				MCPPendingConfirmTool.Empty();
				bClaimed = true;
			}
		}
		if (bClaimed)
		{
			MCPPendingConfirmDecision.Store(1);
			if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));
			if (MCPPendingConfirmEvent) MCPPendingConfirmEvent->Trigger();
			return FReply::Handled();
		}
	}

	if (!bConfirmationPending) return FReply::Handled();

	FString ToolName = PendingConfirmToolName;
	TSharedPtr<FJsonObject> Args = PendingConfirmArguments;
	FString OriginChatID = PendingConfirmChatID;
	bConfirmationPending = false;
	PendingConfirmChatID.Empty();
	if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));
	bBypassAskBeforeEditOnce = true;

	FToolExecutionResult ToolResult = DispatchToolCall(ToolName, Args);

	if (FLearningManager::Get().IsInitialized())
	{
		FLearningManager& LM = FLearningManager::Get();

		if (ToolResult.bSuccess)
		{
			LM.OnToolExecuted(ToolName, Args);
		}

		LM.TrackToolCompletion(ToolName, Args, ToolResult.bSuccess, ToolResult.ResultJson, ToolResult.ErrorMessage, TEXT("widget"));
	}

	if (!ToolResult.bIsPending)
	{
		ContinueConversationWithToolResult(ToolName, ToolResult, OriginChatID);
	}
	return FReply::Handled();
}

FReply SUECPMainWidget::OnConfirmToolSkip()
{
	{
		bool bClaimed = false;
		{
			FScopeLock L(&MCPConfirmTagLock);
			if (!MCPPendingConfirmTool.IsEmpty())
			{
				MCPPendingConfirmTool.Empty();
				bClaimed = true;
			}
		}
		if (bClaimed)
		{
			MCPPendingConfirmDecision.Store(2);
			if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));
			if (MCPPendingConfirmEvent) MCPPendingConfirmEvent->Trigger();
			return FReply::Handled();
		}
	}

	if (!bConfirmationPending) return FReply::Handled();

	FString ToolName = PendingConfirmToolName;
	FString OriginChatID = PendingConfirmChatID;
	bConfirmationPending = false;
	PendingConfirmChatID.Empty();
	if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));

	FToolExecutionResult SkipResult;
	SkipResult.bSuccess = false;
	SkipResult.ErrorMessage = TEXT("The user chose to skip this tool. Continue planning without executing it, or ask what they would like to do differently.");
	ContinueConversationWithToolResult(ToolName, SkipResult, OriginChatID);
	return FReply::Handled();
}

FReply SUECPMainWidget::OnConfirmToolStop()
{
	{
		bool bClaimed = false;
		{
			FScopeLock L(&MCPConfirmTagLock);
			if (!MCPPendingConfirmTool.IsEmpty())
			{
				MCPPendingConfirmTool.Empty();
				bClaimed = true;
			}
		}
		if (bClaimed)
		{
			MCPPendingConfirmDecision.Store(3);
			if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));
			if (MCPPendingConfirmEvent) MCPPendingConfirmEvent->Trigger();
			return FReply::Handled();
		}
	}

	if (!bConfirmationPending) return FReply::Handled();

	FString ToolName = PendingConfirmToolName;
	FString OriginChatID = PendingConfirmChatID;
	bConfirmationPending = false;
	PendingConfirmChatID.Empty();
	if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));

	FToolExecutionResult StopResult;
	StopResult.bSuccess = false;
	StopResult.ErrorMessage = TEXT("The user cancelled this operation. Stop executing tools and ask the user what they would like to do.");
	ContinueConversationWithToolResult(ToolName, StopResult, OriginChatID);
	return FReply::Handled();
}

SUECPMainWidget::EMCPConfirmDecision SUECPMainWidget::RequestMCPDestructiveConfirm(
	const FString& ToolName, const FString& ArgsPreview, double TimeoutSecs)
{
	if (!MCPConfirmSlotMutex.TryLock())
	{
		return EMCPConfirmDecision::Busy;
	}
	ON_SCOPE_EXIT { MCPConfirmSlotMutex.Unlock(); };

	if (!MCPPendingConfirmEvent)
	{
		MCPPendingConfirmEvent = FPlatformProcess::GetSynchEventFromPool( false);
	}
	MCPPendingConfirmEvent->Reset();
	MCPPendingConfirmDecision.Store(0);
	{
		FScopeLock L(&MCPConfirmTagLock);
		MCPPendingConfirmTool = ToolName;
	}

	const FString ToolCopy = ToolName;
	const FString PreviewCopy = ArgsPreview;
	TWeakPtr<SWidget> WeakSelf = AsWeak();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, ToolCopy, PreviewCopy]()
	{
		if (TSharedPtr<SWidget> Pinned = WeakSelf.Pin())
		{
			SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
			if (Self->AppBridgeObject)
				Self->AppBridgeObject->PushToolConfirmation(ToolCopy, PreviewCopy);
		}
	});

	const uint32 TimeoutMs = FMath::Max(1u, (uint32)(TimeoutSecs * 1000.0));
	const bool bSignalled = MCPPendingConfirmEvent->Wait(TimeoutMs);

	if (!bSignalled)
	{
		FString ToolForLog;
		{
			FScopeLock L(&MCPConfirmTagLock);
			ToolForLog = MCPPendingConfirmTool;
			MCPPendingConfirmTool.Empty();
		}
		AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
		{
			if (TSharedPtr<SWidget> Pinned = WeakSelf.Pin())
			{
				SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
				if (Self->AppBridgeObject)
					Self->AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));
			}
		});
		UE_LOG(LogUECPShell, Warning, TEXT("[MCP-confirm] '%s' timed out after %.0fs"), *ToolForLog, TimeoutSecs);
		return EMCPConfirmDecision::Timeout;
	}

	const int32 Dec = MCPPendingConfirmDecision.Load();
	switch (Dec)
	{
	case 1: return EMCPConfirmDecision::Proceed;
	case 2: return EMCPConfirmDecision::Skip;
	case 3: return EMCPConfirmDecision::Stop;
	default: return EMCPConfirmDecision::Timeout;
	}
}

FString SUECPMainWidget::RequestUserQuestions(const FString& QuestionsJson, double TimeoutSecs)
{
	if (IsInGameThread())
	{
		return FString();
	}

	if (!MCPQuestionSlotMutex.TryLock())
	{
		return FString();
	}
	ON_SCOPE_EXIT { MCPQuestionSlotMutex.Unlock(); };

	if (!MCPPendingQuestionEvent)
	{
		MCPPendingQuestionEvent = FPlatformProcess::GetSynchEventFromPool( false);
	}
	MCPPendingQuestionEvent->Reset();
	{
		FScopeLock L(&MCPQuestionTagLock);
		MCPPendingQuestionAnswer.Empty();
		MCPPendingQuestionTag = TEXT("ask_user");
	}

	const FString QuestionsCopy = QuestionsJson;
	TWeakPtr<SWidget> WeakSelf = AsWeak();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, QuestionsCopy]()
	{
		if (TSharedPtr<SWidget> Pinned = WeakSelf.Pin())
		{
			SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
			if (Self->AppBridgeObject)
				Self->AppBridgeObject->PushUserQuestions(QuestionsCopy);
		}
	});

	const uint32 TimeoutMs = FMath::Max(1u, (uint32)(TimeoutSecs * 1000.0));
	const bool bSignalled = MCPPendingQuestionEvent->Wait(TimeoutMs);

	if (!bSignalled)
	{
		{
			FScopeLock L(&MCPQuestionTagLock);
			MCPPendingQuestionTag.Empty();
		}
		AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
		{
			if (TSharedPtr<SWidget> Pinned = WeakSelf.Pin())
			{
				SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
				if (Self->AppBridgeObject)
					Self->AppBridgeObject->ExecJs(TEXT("if(typeof onUserQuestionsDone==='function')onUserQuestionsDone()"));
			}
		});
		UE_LOG(LogUECPShell, Warning, TEXT("[ask_user] question prompt timed out after %.0fs"), TimeoutSecs);
		return FString();
	}

	FString LocalAnswer;
	{
		FScopeLock L(&MCPQuestionTagLock);
		LocalAnswer = MCPPendingQuestionAnswer;
	}
	return LocalAnswer;
}

void SUECPMainWidget::OnUserQuestionsAnswered(const FString& AnswerJson)
{
	{
		FScopeLock L(&MCPQuestionTagLock);
		if (MCPPendingQuestionTag.IsEmpty())
		{
			return;
		}
		MCPPendingQuestionTag.Empty();
		MCPPendingQuestionAnswer = AnswerJson;
	}
	if (MCPPendingQuestionEvent) MCPPendingQuestionEvent->Trigger();
}

#undef LOCTEXT_NAMESPACE
