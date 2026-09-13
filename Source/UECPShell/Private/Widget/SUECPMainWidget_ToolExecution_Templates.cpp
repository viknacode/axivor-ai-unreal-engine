// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "EdGraphUtilities.h"
#include "K2Node_Variable.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Self.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "BlueprintEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ApiKeyManager.h"
#include "Managers/HttpCommunicationManager.h"
#include "Widget/UUECPAppBridge.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

static FString ComposeTemplateString(const TArray<uint8>& Encoded, const FString& Key)
{
	FString Result;
	if (Key.IsEmpty() || Encoded.Num() == 0) return Result;
	TArray<uint8> KB;
	FTCHARToUTF8 Conv(*Key);
	KB.Append((uint8*)Conv.Get(), Conv.Length());
	for (int32 i = 0; i < Encoded.Num(); ++i)
		Result += (TCHAR)(Encoded[i] ^ KB[i % KB.Num()]);
	return Result;
}

static FString BuildTemplateSalt()
{
	const uint8 a[] = { 0x67, 0x43, 0x5F, 0x77, 0x51, 0x6C, 0x01, 0x03, 0x01, 0x05, 0x6C, 0x78, 0x56, 0x4A };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	return K;
}

static FString GetTemplateRegistryBase()
{
	static const TArray<uint8> _tU = {0x3c,0x04,0x18,0x34,0x11,0x65,0x1d,0x1f,0x53,0x46,0x3c,0x2a,0x03,0x1e,0x36,0x02,0x0b,0x2d,0x17,0x3e,0x55,0x49,0x48,0x55,0x37,0x2a,0x03,0x09,0x7a,0x03,0x19,0x34,0x03,0x3d,0x53,0x43,0x57,0x18,0x3c,0x24};
	return ComposeTemplateString(_tU, BuildTemplateSalt());
}

static FString GetTemplateRegistryToken()
{
	static const TArray<uint8> _tK = {0x27,0x12,0x33,0x34,0x17,0x3d,0x5e,0x59,0x41,0x5e,0x3e,0x29,0x09,0x1c,0x0b,0x14,0x5f,0x06,0x33,0x29,0x47,0x76,0x76,0x5d,0x28,0x32,0x21,0x3f,0x60,0x3b,0x22,0x10,0x24,0x39,0x6b,0x7e,0x45,0x69,0x3c,0x1d,0x02,0x40,0x1f,0x1b,0x21,0x3e};
	return ComposeTemplateString(_tK, BuildTemplateSalt());
}

static FString SupabaseRequest(const FString& Verb, const FString& Endpoint,
                                const FString& Body = TEXT(""), double TimeoutSec = 5.0)
{
	FString SupabaseURL = GetTemplateRegistryBase();
	FString AnonKey     = GetTemplateRegistryToken();
	if (SupabaseURL.IsEmpty() || AnonKey.IsEmpty())
	{
		UE_LOG(LogUECPShell, Error, TEXT("RegistryRequest: URL or token is empty! URL=%s TokenLen=%d"), *SupabaseURL, AnonKey.Len());
		return FString();
	}

	FString FullURL = SupabaseURL + Endpoint;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(FullURL);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("apikey"), AnonKey);
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	if (Verb == TEXT("POST"))
	{
		Request->SetHeader(TEXT("Prefer"), TEXT("return=representation"));
		Request->SetContentAsString(Body);
	}

	struct FReqState
	{
		std::atomic<bool> bComplete{false};
		std::atomic<bool> bSuccess{false};
		FString ResponseText;
	};
	TSharedPtr<FReqState> State = MakeShared<FReqState>();

	Request->OnProcessRequestComplete().BindLambda(
		[State](FHttpRequestPtr, FHttpResponsePtr Response, bool bOK)
	{
		if (bOK && Response.IsValid())
		{
			int32 Code = Response->GetResponseCode();
			if (Code == 200 || Code == 201)
			{
				State->ResponseText = Response->GetContentAsString();
				State->bSuccess = true;
			}
			else
			{
				UE_LOG(LogUECPShell, Error, TEXT("RegistryRequest: HTTP %d — %s"), Code, *Response->GetContentAsString().Left(500));
			}
		}
		else
		{
			UE_LOG(LogUECPShell, Error, TEXT("RegistryRequest: Request failed (bOK=%d, Response valid=%d)"), bOK, Response.IsValid());
		}
		State->bComplete = true;
	});

	double StartTime = FPlatformTime::Seconds();
	Request->ProcessRequest();

	while (!State->bComplete && (FPlatformTime::Seconds() - StartTime) < TimeoutSec)
	{
		FPlatformProcess::Sleep(0.05f);
		FSlateApplication::Get().PumpMessages();
	}

	if (!State->bComplete) { Request->CancelRequest(); return FString(); }
	return State->bSuccess ? State->ResponseText : FString();
}

static FString NodeToHandle(UEdGraphNode* Node, FString& OutCustomName)
{
	OutCustomName.Empty();

	if (Cast<UK2Node_Knot>(Node) || Cast<UK2Node_FunctionResult>(Node))
		return FString();

	if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
	{
		OutCustomName = CE->CustomFunctionName.ToString();
		return TEXT("ev.CustomEvent");
	}
	if (UK2Node_Event* Ev = Cast<UK2Node_Event>(Node))
	{
		return FString::Printf(TEXT("ev.%s"), *Ev->EventReference.GetMemberName().ToString());
	}
	if (UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* Func = CF->GetTargetFunction();
		if (Func && Func->GetOwnerClass())
		{
			FString ClassName = Func->GetOwnerClass()->GetName();
			ClassName.RemoveFromStart(TEXT("U"));
			ClassName.RemoveFromStart(TEXT("A"));
			return FString::Printf(TEXT("fn.%s.%s"), *ClassName, *Func->GetName());
		}
		return FString::Printf(TEXT("fn.%s"), *CF->FunctionReference.GetMemberName().ToString());
	}
	if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
	{
		FString VarName = VG->VariableReference.GetMemberName().ToString();
		if (VG->VariableReference.GetMemberParentClass() && VG->VariableReference.IsSelfContext() == false)
		{
			FString ClassName = VG->VariableReference.GetMemberParentClass()->GetName();
			ClassName.RemoveFromStart(TEXT("U"));
			return FString::Printf(TEXT("prop.get.%s.%s"), *ClassName, *VarName);
		}
		return FString::Printf(TEXT("var.get.%s"), *VarName);
	}
	if (UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
	{
		FString VarName = VS->VariableReference.GetMemberName().ToString();
		if (VS->VariableReference.GetMemberParentClass() && VS->VariableReference.IsSelfContext() == false)
		{
			FString ClassName = VS->VariableReference.GetMemberParentClass()->GetName();
			ClassName.RemoveFromStart(TEXT("U"));
			return FString::Printf(TEXT("prop.set.%s.%s"), *ClassName, *VarName);
		}
		return FString::Printf(TEXT("var.set.%s"), *VarName);
	}
	if (Cast<UK2Node_IfThenElse>(Node))          return TEXT("k2.Branch");
	if (Cast<UK2Node_Self>(Node))                return TEXT("k2.Self");
	if (Cast<UK2Node_MakeArray>(Node))           return TEXT("k2.MakeArray");
	if (Cast<UK2Node_ExecutionSequence>(Node))   return TEXT("k2.Sequence");
	if (UK2Node_MacroInstance* MI = Cast<UK2Node_MacroInstance>(Node))
	{
		FString MacroName = MI->GetMacroGraph() ? MI->GetMacroGraph()->GetName() : TEXT("Unknown");
		return FString::Printf(TEXT("mc.%s"), *MacroName);
	}
	if (Cast<UK2Node_FunctionEntry>(Node))
		return FString();

	FString Title = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
	if (Title.IsEmpty()) Title = Node->GetClass()->GetName();
	return FString::Printf(TEXT("k2.%s"), *Title);
}

static TSharedPtr<FJsonObject> ExportNodesToBuildGraphJson(const TSet<UEdGraphNode*>& SelectedNodes)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

	TMap<UEdGraphNode*, FString> NodeIdMap;
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	TArray<TSharedPtr<FJsonValue>> DefaultsArray;

	int32 IdCounter = 0;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (!Node || Cast<UK2Node_Knot>(Node) || Cast<UEdGraphNode_Comment>(Node)) continue;

		FString CustomName;
		FString Handle = NodeToHandle(Node, CustomName);
		if (Handle.IsEmpty()) continue;

		FString NodeId = FString::Printf(TEXT("n%d"), IdCounter++);
		NodeIdMap.Add(Node, NodeId);

		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
		NodeObj->SetStringField(TEXT("id"), NodeId);
		NodeObj->SetStringField(TEXT("handle"), Handle);
		if (!CustomName.IsEmpty())
			NodeObj->SetStringField(TEXT("custom_name"), CustomName);

		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}

	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (!NodeIdMap.Contains(Node)) continue;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				UEdGraphNode* TargetNode = Linked->GetOwningNode();
				while (TargetNode && Cast<UK2Node_Knot>(TargetNode))
				{
					UK2Node_Knot* Knot = Cast<UK2Node_Knot>(TargetNode);
					if (Knot->Pins.Num() >= 2 && Knot->Pins[1]->LinkedTo.Num() > 0)
					{
						Linked = Knot->Pins[1]->LinkedTo[0];
						TargetNode = Linked->GetOwningNode();
					}
					else break;
				}
				if (!TargetNode || !NodeIdMap.Contains(TargetNode)) continue;

				TSharedPtr<FJsonObject> Conn = MakeShareable(new FJsonObject());
				Conn->SetStringField(TEXT("from"), NodeIdMap[Node]);
				Conn->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
				Conn->SetStringField(TEXT("to"), NodeIdMap[TargetNode]);
				Conn->SetStringField(TEXT("to_pin"), Linked->PinName.ToString());
				ConnectionsArray.Add(MakeShareable(new FJsonValueObject(Conn)));
			}
		}
	}

	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (!NodeIdMap.Contains(Node)) continue;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (Pin->LinkedTo.Num() > 0) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			FString Value = Pin->DefaultValue;
			if (Value.IsEmpty() && Pin->DefaultObject)
				Value = Pin->DefaultObject->GetPathName();
			if (!Value.IsEmpty() && Value != TEXT("self"))
			{
				TSharedPtr<FJsonObject> Def = MakeShareable(new FJsonObject());
				Def->SetStringField(TEXT("node_id"), NodeIdMap[Node]);
				Def->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
				Def->SetStringField(TEXT("value"), Value);
				DefaultsArray.Add(MakeShareable(new FJsonValueObject(Def)));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	TSet<FString> SeenVarNames;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		auto AddVar = [&](const FString& VarName, UEdGraphPin* TypePin)
		{
			if (VarName.IsEmpty() || SeenVarNames.Contains(VarName)) return;
			SeenVarNames.Add(VarName);
			TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
			VarObj->SetStringField(TEXT("name"), VarName);
			FString VarType = TEXT("bool");
			if (TypePin)
			{
				FName Cat = TypePin->PinType.PinCategory;
				if (Cat == UEdGraphSchema_K2::PC_Float) VarType = TEXT("float");
				else if (Cat == UEdGraphSchema_K2::PC_Int) VarType = TEXT("int");
				else if (Cat == UEdGraphSchema_K2::PC_Name) VarType = TEXT("name");
				else if (Cat == UEdGraphSchema_K2::PC_String) VarType = TEXT("string");
				else if (Cat == UEdGraphSchema_K2::PC_Text) VarType = TEXT("text");
				else if (Cat == UEdGraphSchema_K2::PC_Struct)
				{
					if (TypePin->PinType.PinSubCategoryObject.IsValid())
						VarType = TypePin->PinType.PinSubCategoryObject->GetName();
				}
				else if (Cat == UEdGraphSchema_K2::PC_Object || Cat == UEdGraphSchema_K2::PC_SoftObject)
				{
					if (TypePin->PinType.PinSubCategoryObject.IsValid())
						VarType = TypePin->PinType.PinSubCategoryObject->GetPathName();
				}
			}
			VarObj->SetStringField(TEXT("type"), VarType);
			VariablesArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
		};

		if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
		{
			if (VG->VariableReference.IsSelfContext())
			{
				UEdGraphPin* OutPin = nullptr;
				for (UEdGraphPin* P : VG->Pins)
					if (P->Direction == EGPD_Output && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) { OutPin = P; break; }
				AddVar(VG->VariableReference.GetMemberName().ToString(), OutPin);
			}
		}
		else if (UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
		{
			if (VS->VariableReference.IsSelfContext())
			{
				UEdGraphPin* InPin = nullptr;
				for (UEdGraphPin* P : VS->Pins)
					if (P->Direction == EGPD_Input && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) { InPin = P; break; }
				AddVar(VS->VariableReference.GetMemberName().ToString(), InPin);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> CommentsArray;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			TSharedPtr<FJsonObject> CommentObj = MakeShareable(new FJsonObject());
			CommentObj->SetStringField(TEXT("text"), Comment->NodeComment);
			TArray<TSharedPtr<FJsonValue>> CoveredIds;
			FVector2D CommentPos(Comment->NodePosX, Comment->NodePosY);
			FVector2D CommentSize(Comment->NodeWidth, Comment->NodeHeight);
			for (auto& KV : NodeIdMap)
			{
				FVector2D NPos(KV.Key->NodePosX, KV.Key->NodePosY);
				if (NPos.X >= CommentPos.X && NPos.X <= CommentPos.X + CommentSize.X &&
					NPos.Y >= CommentPos.Y && NPos.Y <= CommentPos.Y + CommentSize.Y)
				{
					CoveredIds.Add(MakeShareable(new FJsonValueString(KV.Value)));
				}
			}
			CommentObj->SetArrayField(TEXT("node_ids"), CoveredIds);
			CommentsArray.Add(MakeShareable(new FJsonValueObject(CommentObj)));
		}
	}

	if (VariablesArray.Num() > 0)
		Result->SetArrayField(TEXT("variables"), VariablesArray);
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("connections"), ConnectionsArray);
	if (DefaultsArray.Num() > 0)
		Result->SetArrayField(TEXT("defaults"), DefaultsArray);
	if (CommentsArray.Num() > 0)
		Result->SetArrayField(TEXT("comments"), CommentsArray);

	return Result;
}

static FString SerializeResult(const TSharedPtr<FJsonObject>& ResultObject)
{
	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	return ResultString;
}

static FToolExecutionResult MakeErrorResult(const FString& ErrorMessage)
{
	FToolExecutionResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = ErrorMessage;

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
	ResultObject->SetBoolField(TEXT("success"), false);
	ResultObject->SetStringField(TEXT("error"), ErrorMessage);
	Result.ResultJson = SerializeResult(ResultObject);
	return Result;
}

static FToolExecutionResult SaveTemplateToDb(const FString& SanitizedT3D, const FString& Name,
                                              const FString& Description, const FString& Category,
                                              int32 NodeCount)
{
	FString TemplateId = FString::Printf(TEXT("tpl_%s_%lld"),
		*Name.Replace(TEXT(" "), TEXT("_")), FDateTime::Now().ToUnixTimestamp());

	TSharedPtr<FJsonObject> T = MakeShareable(new FJsonObject());
	T->SetStringField(TEXT("id"),          TemplateId);
	T->SetStringField(TEXT("name"),        Name);
	T->SetStringField(TEXT("description"), Description.IsEmpty() ? Name : Description);
	T->SetStringField(TEXT("node_graph"),  SanitizedT3D);
	T->SetNumberField(TEXT("timestamp"),   (double)FDateTime::Now().ToUnixTimestamp());
	T->SetNumberField(TEXT("node_count"),  NodeCount);
	T->SetStringField(TEXT("category"),    Category.IsEmpty() ? TEXT("General") : Category);
	T->SetStringField(TEXT("tags"),        TEXT(""));

	FString PostBody;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&PostBody);
	FJsonSerializer::Serialize(T.ToSharedRef(), W);

	FString Response = SupabaseRequest(TEXT("POST"), TEXT("/rest/v1/blueprint_templates"), PostBody, 10.0);
	if (Response.IsEmpty())
		return MakeErrorResult(TEXT("Failed to save template to server. Check network connection."));

	FToolExecutionResult Result;
	Result.bSuccess = true;
	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject());
	Out->SetBoolField(TEXT("success"),     true);
	Out->SetStringField(TEXT("solution_id"), TemplateId);
	Out->SetStringField(TEXT("message"),   FString::Printf(TEXT("Template exported: '%s' (%d nodes, %s)"), *Name, NodeCount, *Category));
	Out->SetNumberField(TEXT("node_count"), NodeCount);
	Result.ResultJson = SerializeResult(Out);
	return Result;
}

static FString ExtractApiResponseText(const FString& Body, const FString& Provider)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(R, Json) || !Json.IsValid()) return FString();

	if (Provider == TEXT("Gemini"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Candidates;
		if (Json->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* C; const TSharedPtr<FJsonObject>* Cnt;
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			const TSharedPtr<FJsonObject>* P;
			FString Text;
			if ((*Candidates)[0]->TryGetObject(C) && (*C)->TryGetObjectField(TEXT("content"), Cnt) &&
				(*Cnt)->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0 &&
				(*Parts)[0]->TryGetObject(P) && (*P)->TryGetStringField(TEXT("text"), Text))
				return Text;
		}
	}
	else if (Provider == TEXT("Claude"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Content;
		if (Json->TryGetArrayField(TEXT("content"), Content) && Content->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* C; FString Text;
			if ((*Content)[0]->TryGetObject(C) && (*C)->TryGetStringField(TEXT("text"), Text))
				return Text;
		}
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* Choices;
		if (Json->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* Ch; const TSharedPtr<FJsonObject>* Msg; FString Text;
			if ((*Choices)[0]->TryGetObject(Ch) && (*Ch)->TryGetObjectField(TEXT("message"), Msg) &&
				(*Msg)->TryGetStringField(TEXT("content"), Text))
				return Text;
		}
	}
	return FString();
}

FString SUECPMainWidget::AutoDetectCategory(const FString& Name, const FString& Description)
{
	FString Combined = (Name + TEXT(" ") + Description).ToLower();
	struct FCategoryMapping { FString Keyword; FString Category; };
	static TArray<FCategoryMapping> Categories = {
		{TEXT("animation"),TEXT("Animation")},{TEXT("anim"),TEXT("Animation")},{TEXT("pose"),TEXT("Animation")},{TEXT("montage"),TEXT("Animation")},
		{TEXT("audio"),TEXT("Audio")},{TEXT("sound"),TEXT("Audio")},{TEXT("music"),TEXT("Audio")},
		{TEXT("behavior"),TEXT("AI")},{TEXT("patrol"),TEXT("AI")},{TEXT("chase"),TEXT("AI")},{TEXT("flee"),TEXT("AI")},{TEXT("sense"),TEXT("AI")},
		{TEXT("widget"),TEXT("UI")},{TEXT("button"),TEXT("UI")},{TEXT("menu"),TEXT("UI")},{TEXT("hud"),TEXT("UI")},
		{TEXT("collision"),TEXT("Physics")},{TEXT("overlap"),TEXT("Physics")},{TEXT("trace"),TEXT("Physics")},{TEXT("hit"),TEXT("Physics")},
		{TEXT("controller"),TEXT("Input")},{TEXT("keyboard"),TEXT("Input")},{TEXT("mouse"),TEXT("Input")},
		{TEXT("variable"),TEXT("Data")},{TEXT("array"),TEXT("Data")},{TEXT("struct"),TEXT("Data")},{TEXT("map"),TEXT("Data")},
		{TEXT("combat"),TEXT("Combat")},{TEXT("damage"),TEXT("Combat")},{TEXT("attack"),TEXT("Combat")},{TEXT("health"),TEXT("Combat")},{TEXT("enemy"),TEXT("Combat")},{TEXT("weapon"),TEXT("Combat")},{TEXT("death"),TEXT("Combat")},{TEXT("block"),TEXT("Combat")},{TEXT("parry"),TEXT("Combat")},{TEXT("dodge"),TEXT("Combat")},
		{TEXT("movement"),TEXT("Movement")},{TEXT("walk"),TEXT("Movement")},{TEXT("run"),TEXT("Movement")},{TEXT("jump"),TEXT("Movement")},{TEXT("fly"),TEXT("Movement")},{TEXT("rotate"),TEXT("Movement")},{TEXT("turn"),TEXT("Movement")},
		{TEXT("ui"),TEXT("UI")},{TEXT("text"),TEXT("UI")},{TEXT("label"),TEXT("UI")},
		{TEXT("ai"),TEXT("AI")},{TEXT("bot"),TEXT("AI")},{TEXT("npc"),TEXT("AI")},
		{TEXT("input"),TEXT("Input")},{TEXT("key"),TEXT("Input")},{TEXT("press"),TEXT("Input")},
		{TEXT("data"),TEXT("Data")},{TEXT("save"),TEXT("Data")},{TEXT("load"),TEXT("Data")},
		{TEXT("physics"),TEXT("Physics")},{TEXT("force"),TEXT("Physics")},{TEXT("impact"),TEXT("Physics")},{TEXT("angle"),TEXT("Physics")},
		{TEXT("spawn"),TEXT("Spawning")},{TEXT("create"),TEXT("Spawning")},{TEXT("destroy"),TEXT("Spawning")},{TEXT("despawn"),TEXT("Spawning")},
		{TEXT("camera"),TEXT("Camera")},{TEXT("view"),TEXT("Camera")}
	};
	for (const FCategoryMapping& Mapping : Categories)
	{
		if (Combined.Contains(Mapping.Keyword)) return Mapping.Category;
	}
	return TEXT("General");
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_ExportBlueprintTemplate(const TSharedPtr<FJsonObject>& Args)
{
	FString TemplateName;
	if (!Args->TryGetStringField(TEXT("name"), TemplateName))
	{
		return MakeErrorResult(TEXT("Missing required parameter: name"));
	}
	FString TemplateDescription;
	Args->TryGetStringField(TEXT("description"), TemplateDescription);

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return MakeErrorResult(TEXT("Could not get the Asset Editor Subsystem."));
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
		return MakeErrorResult(TEXT("No active asset editor found. Please open a Blueprint."));
	}

	const FName EditorName = ActiveEditor->GetEditorName();
	if (EditorName != FName(TEXT("BlueprintEditor")) && EditorName != FName(TEXT("AnimationBlueprintEditor")) && EditorName != FName(TEXT("WidgetBlueprintEditor")))
	{
		return MakeErrorResult(TEXT("Active editor is not a Blueprint editor."));
	}

	FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor);
	UBlueprint* Blueprint = BlueprintEditor->GetBlueprintObj();
	if (!Blueprint)
	{
		return MakeErrorResult(TEXT("Could not get Blueprint from editor."));
	}

	TSet<UEdGraphNode*> AllSelectedNodes;
	const TSet<UObject*> EditorSelectedNodes = BlueprintEditor->GetSelectedNodes();
	for (UObject* Obj : EditorSelectedNodes)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
		{
			AllSelectedNodes.Add(Node);
		}
	}

	if (AllSelectedNodes.Num() == 0)
	{
		return MakeErrorResult(TEXT("No nodes selected. Please select the nodes you want to export as a template."));
	}

	TSharedPtr<FJsonObject> BuildGraphJson = ExportNodesToBuildGraphJson(AllSelectedNodes);
	if (!BuildGraphJson.IsValid())
		return MakeErrorResult(TEXT("Failed to convert selected nodes to build_graph format."));

	const TArray<TSharedPtr<FJsonValue>>* ExportedNodes = nullptr;
	BuildGraphJson->TryGetArrayField(TEXT("nodes"), ExportedNodes);
	int32 NodeCount = ExportedNodes ? ExportedNodes->Num() : 0;

	FString BuildGraphStr;
	TSharedRef<TJsonWriter<>> BGW = TJsonWriterFactory<>::Create(&BuildGraphStr);
	FJsonSerializer::Serialize(BuildGraphJson.ToSharedRef(), BGW);

	FString Category;
	Args->TryGetStringField(TEXT("category"), Category);
	if (Category.IsEmpty())
		Category = AutoDetectCategory(TemplateName, TemplateDescription.IsEmpty() ? TemplateName : TemplateDescription);

	return SaveTemplateToDb(BuildGraphStr, TemplateName,
		TemplateDescription.IsEmpty() ? TemplateName : TemplateDescription,
		Category, NodeCount);
}

void SUECPMainWidget::FetchTemplateCacheAsync()
{
	if (bTemplatesCacheFetched && CachedTemplateMetadata.Num() > 0) return;

	FString SupabaseURL = GetTemplateRegistryBase();
	FString AnonKey     = GetTemplateRegistryToken();
	if (SupabaseURL.IsEmpty() || AnonKey.IsEmpty())
	{
		UE_LOG(LogUECPShell, Error, TEXT("FetchTemplateCacheAsync: Remote base or token is empty!"));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(SupabaseURL + TEXT("/rest/v1/blueprint_templates?select=id,name,description,category,tags,node_count,node_graph"));
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("apikey"), AnonKey);
	Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetTimeout(10.0f);

	SUECPMainWidget* RawSelf = this;
	Req->OnProcessRequestComplete().BindLambda(
		[RawSelf](FHttpRequestPtr, FHttpResponsePtr Response, bool bOK)
	{
		AsyncTask(ENamedThreads::GameThread, [RawSelf, Response, bOK]()
		{
			if (bOK && Response.IsValid() && Response->GetResponseCode() == 200)
			{
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FJsonSerializer::Deserialize(R, RawSelf->CachedTemplateMetadata);
			}
			else
			{
				FString Detail = (Response.IsValid()) ? FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode()) : TEXT("no response");
				UE_LOG(LogUECPShell, Error, TEXT("Template cache: FAILED to fetch (%s)"), *Detail);
			}
			RawSelf->bTemplatesCacheFetched = true;
		});
	});
	Req->ProcessRequest();
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_SearchBlueprintTemplates(const TSharedPtr<FJsonObject>& Args)
{
	FString Query;
	if (!Args->TryGetStringField(TEXT("query"), Query))
		return MakeErrorResult(TEXT("Missing required parameter: query"));

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	if (CachedTemplateMetadata.Num() == 0)
	{
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetNumberField(TEXT("count"), 0);
		ResultObject->SetArrayField(TEXT("solutions"), TArray<TSharedPtr<FJsonValue>>());
		ResultObject->SetStringField(TEXT("message"), TEXT("No templates available. Will generate from scratch."));
		FToolExecutionResult Result;
		Result.bSuccess = true;
		Result.ResultJson = SerializeResult(ResultObject);
		return Result;
	}

	FString QueryLower = Query.ToLower();
	TArray<FString> QueryWords;
	QueryLower.ParseIntoArray(QueryWords, TEXT(" "), true);

	TArray<TPair<int32, TSharedPtr<FJsonObject>>> ScoredMatches;
	for (const TSharedPtr<FJsonValue>& Val : CachedTemplateMetadata)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Val->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;

		FString Name, Description, Tags, Category;
		(*Obj)->TryGetStringField(TEXT("name"), Name);
		(*Obj)->TryGetStringField(TEXT("description"), Description);
		(*Obj)->TryGetStringField(TEXT("tags"), Tags);
		(*Obj)->TryGetStringField(TEXT("category"), Category);

		FString SearchText = (Name + TEXT(" ") + Description + TEXT(" ") + Tags + TEXT(" ") + Category).ToLower();
		int32 Score = 0;
		for (const FString& Word : QueryWords)
			if (SearchText.Contains(Word)) Score++;

		if (Score > 0)
		{
			TSharedPtr<FJsonObject> Match = MakeShareable(new FJsonObject());
			FString Id; (*Obj)->TryGetStringField(TEXT("id"), Id);
			Match->SetStringField(TEXT("solution_id"), Id);
			Match->SetStringField(TEXT("name"), Name);
			Match->SetStringField(TEXT("description"), Description);
			Match->SetStringField(TEXT("category"), Category);
			double NodeCount = 0.0; (*Obj)->TryGetNumberField(TEXT("node_count"), NodeCount);
			Match->SetNumberField(TEXT("node_count"), NodeCount);
			ScoredMatches.Add(TPair<int32, TSharedPtr<FJsonObject>>(Score, Match));
		}
	}

	ScoredMatches.Sort([](const TPair<int32, TSharedPtr<FJsonObject>>& A, const TPair<int32, TSharedPtr<FJsonObject>>& B) {
		return A.Key > B.Key;
	});

	TArray<TSharedPtr<FJsonValue>> MatchedTemplates;
	for (const auto& Pair : ScoredMatches)
		MatchedTemplates.Add(MakeShareable(new FJsonValueObject(Pair.Value)));

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetNumberField(TEXT("count"), MatchedTemplates.Num());
	ResultObject->SetArrayField(TEXT("solutions"), MatchedTemplates);
	ResultObject->SetStringField(TEXT("message"),
		MatchedTemplates.Num() > 0
			? FString::Printf(TEXT("Found %d template(s) matching '%s'."), MatchedTemplates.Num(), *Query)
			: FString::Printf(TEXT("No templates found matching '%s'. Will generate code instead."), *Query));

	FToolExecutionResult Result;
	Result.bSuccess = true;
	Result.ResultJson = SerializeResult(ResultObject);
	return Result;
}

static FString ExtractEntryName(const FString& NodeGraphText)
{
	int32 FuncEntryPos = NodeGraphText.Find(TEXT("K2Node_FunctionEntry"));
	if (FuncEntryPos != INDEX_NONE)
	{
		int32 MemberNamePos = NodeGraphText.Find(TEXT("MemberName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, FuncEntryPos);
		if (MemberNamePos != INDEX_NONE && MemberNamePos < FuncEntryPos + 500)
		{
			int32 NameStart = MemberNamePos + 12;
			int32 NameEnd = NodeGraphText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
			if (NameEnd != INDEX_NONE)
			{
				return NodeGraphText.Mid(NameStart, NameEnd - NameStart);
			}
		}
	}
	int32 CustomPos = NodeGraphText.Find(TEXT("CustomFunctionName=\""));
	if (CustomPos != INDEX_NONE)
	{
		int32 NameStart = CustomPos + 20;
		int32 NameEnd = NodeGraphText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
		if (NameEnd != INDEX_NONE)
		{
			return NodeGraphText.Mid(NameStart, NameEnd - NameStart);
		}
	}
	return FString();
}

static FString ConvertFunctionToEvent(const FString& NodeGraphText, const FString& EntryName)
{
	FString Result = NodeGraphText;

	Result = Result.Replace(TEXT("K2Node_FunctionEntry"), TEXT("K2Node_CustomEvent"));

	FString OldRef = FString::Printf(TEXT("FunctionReference=(MemberName=\"%s\")"), *EntryName);
	FString NewRef = FString::Printf(TEXT("CustomFunctionName=\"%s\""), *EntryName);
	Result = Result.Replace(*OldRef, *NewRef);

	TArray<FString> Lines;
	Result.ParseIntoArray(Lines, TEXT("\n"), false);
	FString Cleaned;
	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStart();
		if (Trimmed.StartsWith(TEXT("ExtraFlags=")) || Trimmed.StartsWith(TEXT("bIsEditable=")))
		{
			continue;
		}
		Cleaned += Line + TEXT("\n");
	}

	while (true)
	{
		int32 ResultStart = Cleaned.Find(TEXT("Begin Object Class=/Script/BlueprintGraph.K2Node_FunctionResult"));
		if (ResultStart == INDEX_NONE) break;
		int32 ResultEnd = Cleaned.Find(TEXT("End Object"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ResultStart);
		if (ResultEnd == INDEX_NONE) break;
		ResultEnd += 10;
		if (ResultEnd < Cleaned.Len() && Cleaned[ResultEnd] == '\n') ResultEnd++;
		Cleaned = Cleaned.Left(ResultStart) + Cleaned.Mid(ResultEnd);
	}

	return Cleaned;
}

static FString ConvertEventToFunction(const FString& NodeGraphText, const FString& EntryName)
{
	FString Result = NodeGraphText;

	Result = Result.Replace(TEXT("K2Node_CustomEvent"), TEXT("K2Node_FunctionEntry"));

	FString OldRef = FString::Printf(TEXT("CustomFunctionName=\"%s\""), *EntryName);
	FString NewRef = FString::Printf(TEXT("FunctionReference=(MemberName=\"%s\")\n   bIsEditable=True"), *EntryName);
	Result = Result.Replace(*OldRef, *NewRef);

	return Result;
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_ApplyBlueprintTemplate(const TSharedPtr<FJsonObject>& Args)
{
	FString SolutionId;
	if (!Args->TryGetStringField(TEXT("solution_id"), SolutionId))
		return MakeErrorResult(TEXT("Missing required parameter: solution_id"));
	FString TargetBlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), TargetBlueprintPath))
		return MakeErrorResult(TEXT("Missing required parameter: blueprint_path"));

	FString NodeGraphText;
	for (const TSharedPtr<FJsonValue>& Val : CachedTemplateMetadata)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Val->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
		FString Id;
		(*Obj)->TryGetStringField(TEXT("id"), Id);
		if (Id == SolutionId)
		{
			(*Obj)->TryGetStringField(TEXT("node_graph"), NodeGraphText);
			break;
		}
	}
	if (NodeGraphText.IsEmpty())
		return MakeErrorResult(FString::Printf(TEXT("Template '%s' not found in cache. Try reopening the plugin to refresh."), *SolutionId));

	TSharedPtr<FJsonObject> BuildGraphArgs;
	TSharedRef<TJsonReader<>> BGReader = TJsonReaderFactory<>::Create(NodeGraphText);
	if (!FJsonSerializer::Deserialize(BGReader, BuildGraphArgs) || !BuildGraphArgs.IsValid())
		return MakeErrorResult(TEXT("Template node_graph is not valid build_graph JSON."));

	BuildGraphArgs->SetStringField(TEXT("blueprint_path"), TargetBlueprintPath);

	const TArray<TSharedPtr<FJsonValue>>* VarsArray = nullptr;
	if (BuildGraphArgs->TryGetArrayField(TEXT("variables"), VarsArray))
	{
		for (const TSharedPtr<FJsonValue>& VarVal : *VarsArray)
		{
			const TSharedPtr<FJsonObject>* VarObj = nullptr;
			if (!VarVal->TryGetObject(VarObj)) continue;
			FString VarName, VarType;
			(*VarObj)->TryGetStringField(TEXT("name"), VarName);
			(*VarObj)->TryGetStringField(TEXT("type"), VarType);
			if (VarName.IsEmpty() || VarType.IsEmpty()) continue;

			TSharedPtr<FJsonObject> AddVarArgs = MakeShareable(new FJsonObject());
			AddVarArgs->SetStringField(TEXT("blueprint_path"), TargetBlueprintPath);
			AddVarArgs->SetStringField(TEXT("variable_name"), VarName);
			AddVarArgs->SetStringField(TEXT("variable_type"), VarType);
			if (IUECPCoreModule::IsAvailable())
			{
				IUECPCoreModule::Get().GetToolDispatcher().ExecuteFromArgs(TEXT("add_variable"), AddVarArgs);
			}
		}
	}

	FToolExecutionResult Result;
	if (IUECPCoreModule::IsAvailable())
	{
		const FUECPToolResult R = IUECPCoreModule::Get().GetToolDispatcher()
			.ExecuteFromArgs(TEXT("build_blueprint_graph"), BuildGraphArgs);
		Result.ResultJson    = R.ResultJson;
		Result.ErrorMessage  = R.ErrorMessage;
		Result.SummaryJson   = R.SummaryJson;
	}
	else
	{
		Result.ErrorMessage = TEXT("UECPCore unavailable - cannot dispatch build_blueprint_graph");
	}
	Result.bSuccess = Result.ErrorMessage.IsEmpty();
	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> ResultObj;
		TSharedRef<TJsonReader<>> RR2 = TJsonReaderFactory<>::Create(Result.ResultJson);
		if (FJsonSerializer::Deserialize(RR2, ResultObj) && ResultObj.IsValid())
		{
			FString Msg;
			ResultObj->TryGetStringField(TEXT("message"), Msg);
			ResultObj->SetStringField(TEXT("message"), FString::Printf(TEXT("Template '%s' applied. %s"), *SolutionId, *Msg));
			ResultObj->SetStringField(TEXT("template_applied"), SolutionId);
			Result.ResultJson = SerializeResult(ResultObj);
		}
	}
	return Result;
}

#if 0
	UBlueprint* TargetBlueprint = LoadObject<UBlueprint>(nullptr, *TargetBlueprintPath);
	if (!TargetBlueprint)
	{
		return MakeErrorResult(FString::Printf(TEXT("Could not load Blueprint at '%s'."), *TargetBlueprintPath));
	}

	bool bHasFunctionEntry = NodeGraphText.Contains(TEXT("K2Node_FunctionEntry"));
	bool bHasEventNode = NodeGraphText.Contains(TEXT("K2Node_Event"));
	bool bHasCustomEvent = NodeGraphText.Contains(TEXT("K2Node_CustomEvent"));

	FString EntryName = ExtractEntryName(NodeGraphText);
	bool bConvertedToEvent = false;
	bool bConvertedToFunction = false;

	if (GraphType == TEXT("event") && bHasFunctionEntry)
	{
		NodeGraphText = ConvertFunctionToEvent(NodeGraphText, EntryName);
		bHasFunctionEntry = false;
		bHasCustomEvent = true;
		bConvertedToEvent = true;
	}
	else if (GraphType == TEXT("function") && (bHasCustomEvent || (bHasEventNode && !bHasFunctionEntry)))
	{
		if (bHasCustomEvent)
		{
			NodeGraphText = ConvertEventToFunction(NodeGraphText, EntryName);
			bHasCustomEvent = false;
			bHasFunctionEntry = true;
			bConvertedToFunction = true;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Apply Blueprint Template")));
	TargetBlueprint->Modify();

	UEdGraph* TargetGraph = nullptr;
	FString GraphName;

	if (bHasFunctionEntry && GraphType != TEXT("event"))
	{
		FString FunctionName = EntryName;
		if (FunctionName.IsEmpty())
		{
			return MakeErrorResult(TEXT("Could not extract function name from template."));
		}

		GraphName = FunctionName;

		for (UEdGraph* Graph : TargetBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName().ToString() == FunctionName)
			{
				TargetGraph = Graph;
				break;
			}
		}
		if (!TargetGraph)
		{
			FName UniqueGraphName = FBlueprintEditorUtils::FindUniqueKismetName(TargetBlueprint, FunctionName);
			TargetGraph = NewObject<UEdGraph>(TargetBlueprint, UniqueGraphName, RF_Transactional);
			TargetGraph->Schema = UEdGraphSchema_K2::StaticClass();
			TargetBlueprint->FunctionGraphs.Add(TargetGraph);
		}

		if (!TargetGraph)
		{
			return MakeErrorResult(FString::Printf(TEXT("Failed to create or find function '%s'."), *FunctionName));
		}

	}
	else
	{
		TargetGraph = FBlueprintEditorUtils::FindEventGraph(TargetBlueprint);
		GraphName = TEXT("EventGraph");
		if (!TargetGraph)
		{
			return MakeErrorResult(TEXT("Could not find Event Graph in target Blueprint."));
		}
	}

	TargetGraph->Modify();

	TSet<UEdGraphNode*> ImportedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, NodeGraphText, ImportedNodes);

	if (ImportedNodes.Num() == 0)
	{
		return MakeErrorResult(TEXT("Failed to import nodes from template. The template format may be invalid."));
	}

	TSet<FString> CreatedVariables;
	for (UEdGraphNode* Node : ImportedNodes)
	{
		if (!Node) continue;

		if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
		{
			FString VarName = VarNode->VariableReference.GetMemberName().ToString();
			FEdGraphPinType VarType;
			VarType.PinCategory = NAME_None;

			for (UEdGraphPin* Pin : VarNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output)
				{
					VarType = Pin->PinType;
					break;
				}
			}

			if (VarType.PinCategory != NAME_None && VarType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				if (!CreatedVariables.Contains(VarName))
				{
					bool bVarExists = false;
					for (const FBPVariableDescription& VarDesc : TargetBlueprint->NewVariables)
					{
						if (VarDesc.VarName.ToString() == VarName)
						{
							bVarExists = true;
							break;
						}
					}
					if (!bVarExists)
					{
						FBlueprintEditorUtils::AddMemberVariable(TargetBlueprint, FName(*VarName), VarType, FString());
						CreatedVariables.Add(VarName);
					}
				}
			}
		}

		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->ReconstructNode();
	}

	int32 LastEventY = MIN_int32;
	for (UEdGraphNode* ExNode : TargetGraph->Nodes)
	{
		if (!ExNode || ImportedNodes.Contains(ExNode)) continue;
		if (ExNode->IsA<UK2Node_Event>() || ExNode->IsA<UK2Node_CustomEvent>())
			LastEventY = FMath::Max(LastEventY, ExNode->NodePosY);
	}

	int32 ChainBottomY = MIN_int32;
	for (UEdGraphNode* ExNode : TargetGraph->Nodes)
	{
		if (!ExNode || ImportedNodes.Contains(ExNode)) continue;
		if (LastEventY == MIN_int32 || ExNode->NodePosY >= LastEventY - 50)
			ChainBottomY = FMath::Max(ChainBottomY, ExNode->NodePosY);
	}

	int32 MinImportedY = MAX_int32;
	for (UEdGraphNode* Node : ImportedNodes)
		if (Node) MinImportedY = FMath::Min(MinImportedY, Node->NodePosY);

	if (ChainBottomY != MIN_int32 && MinImportedY != MAX_int32)
	{
		const int32 OffsetY = (ChainBottomY + 600) - MinImportedY;
		for (UEdGraphNode* Node : ImportedNodes)
			if (Node) Node->NodePosY += OffsetY;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
	TargetBlueprint->PostEditChange();

	FToolExecutionResult Result;
	Result.bSuccess = true;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("graph_name"), GraphName);
	ResultObject->SetNumberField(TEXT("nodes_imported"), ImportedNodes.Num());
	if (CreatedVariables.Num() > 0)
	{
		ResultObject->SetNumberField(TEXT("variables_created"), CreatedVariables.Num());
	}
	FString Message = FString::Printf(TEXT("Template applied successfully. Imported %d nodes to %s."), ImportedNodes.Num(), *GraphName);
	if (bConvertedToEvent)
	{
		Message += TEXT(" (Converted from function to custom event)");
	}
	else if (bConvertedToFunction)
	{
		Message += TEXT(" (Converted from custom event to function)");
	}
	if (CreatedVariables.Num() > 0)
	{
		Message += FString::Printf(TEXT(" Created %d missing variable(s)."), CreatedVariables.Num());
	}
	ResultObject->SetStringField(TEXT("message"), Message);
	Result.ResultJson = SerializeResult(ResultObject);
	return Result;
}
#endif

FReply SUECPMainWidget::OnExportTemplateClicked()
{
	if (bIsExportingTemplate) return FReply::Handled();

	UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AES)
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: No Asset Editor Subsystem."), TEXT("error"));
		return FReply::Handled();
	}

	IAssetEditorInstance* ActiveEditor = nullptr;
	double LastTime = 0.0;
	for (UObject* Asset : AES->GetAllEditedAssets())
	{
		for (IAssetEditorInstance* Ed : AES->FindEditorsForAsset(Asset))
		{
			if (Ed && Ed->GetLastActivationTime() > LastTime)
			{
				LastTime = Ed->GetLastActivationTime();
				ActiveEditor = Ed;
			}
		}
	}

	if (!ActiveEditor)
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: No active Blueprint editor found."), TEXT("error"));
		return FReply::Handled();
	}

	const FName EdName = ActiveEditor->GetEditorName();
	if (EdName != FName(TEXT("BlueprintEditor")) && EdName != FName(TEXT("AnimationBlueprintEditor")) && EdName != FName(TEXT("WidgetBlueprintEditor")))
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: Active editor is not a Blueprint editor."), TEXT("error"));
		return FReply::Handled();
	}

	FBlueprintEditor* BPEd = static_cast<FBlueprintEditor*>(ActiveEditor);
	UBlueprint* Blueprint = BPEd->GetBlueprintObj();
	if (!Blueprint)
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: Could not get Blueprint from editor."), TEXT("error"));
		return FReply::Handled();
	}

	TSet<UEdGraphNode*> SelectedNodes;
	for (UObject* Obj : BPEd->GetSelectedNodes())
		if (UEdGraphNode* N = Cast<UEdGraphNode>(Obj)) SelectedNodes.Add(N);

	if (SelectedNodes.IsEmpty())
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: No nodes selected — select nodes in the BP editor first."), TEXT("error"));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> BuildGraphJson = ExportNodesToBuildGraphJson(SelectedNodes);
	if (!BuildGraphJson.IsValid())
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: Failed to convert nodes to build_graph format."), TEXT("error"));
		return FReply::Handled();
	}

	FString BuildGraphStr;
	TSharedRef<TJsonWriter<>> BGW = TJsonWriterFactory<>::Create(&BuildGraphStr);
	FJsonSerializer::Serialize(BuildGraphJson.ToSharedRef(), BGW);

	const TArray<TSharedPtr<FJsonValue>>* ExpNodes = nullptr;
	BuildGraphJson->TryGetArrayField(TEXT("nodes"), ExpNodes);
	const int32 NodeCount = ExpNodes ? ExpNodes->Num() : SelectedNodes.Num();

	FString Prompt = FString::Printf(
		TEXT("Analyze this Unreal Engine Blueprint node graph (build_graph JSON format) and return ONLY a valid JSON object with these fields:\n")
		TEXT("- \"name\": a concise 2-5 word title for this logic pattern\n")
		TEXT("- \"category\": exactly one of Animation|Audio|AI|UI|Physics|Input|Data|Combat|Movement|Spawning|Camera|General\n")
		TEXT("- \"description\": 1-2 sentences explaining what this logic does\n\n")
		TEXT("Respond with ONLY the JSON object, no explanation, no markdown fences.\n\n")
		TEXT("build_graph:\n%s"),
		*BuildGraphStr
	);

	FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();
	FString ApiKey    = Slot.ApiKey;
	FString Provider  = Slot.Provider;
	FString BaseURL   = Slot.CustomBaseURL;
	FString ModelName = Slot.CustomModelName;

	if (ApiKey.IsEmpty() && Provider != TEXT("Custom"))
	{
		if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Template Export: No API key configured."), TEXT("error"));
		return FReply::Handled();
	}

	bIsExportingTemplate = true;
	if (AppBridgeObject) AppBridgeObject->PushToast(TEXT("Analyzing nodes with AI..."), TEXT("info"));

	TSharedPtr<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetTimeout(45.0f);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject());

	auto MakeUserMessage = [&](const FString& Content) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Msgs;
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject());
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		Msg->SetStringField(TEXT("content"), Content);
		Msgs.Add(MakeShareable(new FJsonValueObject(Msg)));
		return Msgs;
	};

	if (Provider == TEXT("Gemini"))
	{
		FString GemModel = FApiKeyManager::Get().GetActiveGeminiModel();
		Request->SetURL(FHttpCommunicationManager::BuildGeminiUrl(GemModel, ApiKey));
		TArray<TSharedPtr<FJsonValue>> Contents;
		TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject());
		C->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> Parts;
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject());
		P->SetStringField(TEXT("text"), Prompt);
		Parts.Add(MakeShareable(new FJsonValueObject(P)));
		C->SetArrayField(TEXT("parts"), Parts);
		Contents.Add(MakeShareable(new FJsonValueObject(C)));
		Payload->SetArrayField(TEXT("contents"), Contents);
	}
	else if (Provider == TEXT("Claude"))
	{
		FString ClModel = FApiKeyManager::Get().GetActiveClaudeModel();
		if (ClModel.IsEmpty()) ClModel = TEXT("claude-3-5-haiku-20241022");
		Request->SetURL(FHttpCommunicationManager::BuildClaudeUrl());
		Request->SetHeader(TEXT("x-api-key"), ApiKey);
		Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
		Payload->SetStringField(TEXT("model"), ClModel);
		Payload->SetNumberField(TEXT("max_tokens"), 300);
		Payload->SetArrayField(TEXT("messages"), MakeUserMessage(Prompt));
	}
	else if (Provider == TEXT("OpenAI"))
	{
		FString OAIModel = FApiKeyManager::Get().GetActiveOpenAIModel();
		if (OAIModel.IsEmpty()) OAIModel = TEXT("gpt-4o-mini");
		Request->SetURL(FHttpCommunicationManager::BuildOpenAIUrl());
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		Payload->SetStringField(TEXT("model"), OAIModel);
		Payload->SetArrayField(TEXT("messages"), MakeUserMessage(Prompt));
	}
	else
	{
		FString URL = BaseURL.TrimStartAndEnd();
		if (URL.IsEmpty()) URL = Provider == TEXT("DeepSeek") ? FHttpCommunicationManager::BuildDeepSeekUrl() : FHttpCommunicationManager::DefaultOpenAIUrl;
		Request->SetURL(URL);
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		FString Model = ModelName.TrimStartAndEnd();
		if (Model.IsEmpty()) Model = Provider == TEXT("DeepSeek") ? TEXT("deepseek-v4-flash") : TEXT("gpt-4o-mini");
		Payload->SetStringField(TEXT("model"), Model);
		Payload->SetArrayField(TEXT("messages"), MakeUserMessage(Prompt));
	}

	FString Body;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), W);
	Request->SetContentAsString(Body);

	TWeakPtr<SUECPMainWidget> WeakSelf = SharedThis(this);
	FString CapturedT3D   = BuildGraphStr;
	FString CapturedProv  = Provider;
	int32   CapturedCount = NodeCount;

	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, CapturedT3D, CapturedProv, CapturedCount]
		(FHttpRequestPtr, FHttpResponsePtr Response, bool bOK)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakSelf, CapturedT3D, CapturedProv, CapturedCount, Response, bOK]()
		{
			TSharedPtr<SUECPMainWidget> Self = WeakSelf.Pin();
			if (!Self.IsValid()) return;
			Self->bIsExportingTemplate = false;

			auto ShowNote = [&Self](const FString& Msg, bool bError = false)
			{
				if (Self->AppBridgeObject)
					Self->AppBridgeObject->PushToast(Msg, bError ? TEXT("error") : TEXT("success"));
			};

			if (!bOK || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				ShowNote(TEXT("Template Export: AI request failed. Check your API key / connection."), true);
				return;
			}

			FString Text = ExtractApiResponseText(Response->GetContentAsString(), CapturedProv).TrimStartAndEnd();

			if (Text.StartsWith(TEXT("```")))
			{
				int32 Start = Text.Find(TEXT("{"));
				int32 End   = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (Start != INDEX_NONE && End != INDEX_NONE && End > Start)
					Text = Text.Mid(Start, End - Start + 1);
			}

			TSharedPtr<FJsonObject> Meta;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(R, Meta) || !Meta.IsValid())
			{
				ShowNote(FString::Printf(TEXT("Template Export: Could not parse AI response as JSON.\n\n%s"), *Text.Left(200)), true);
				return;
			}

			FString Name, Category, Description;
			Meta->TryGetStringField(TEXT("name"),        Name);
			Meta->TryGetStringField(TEXT("category"),    Category);
			Meta->TryGetStringField(TEXT("description"), Description);

			if (Name.IsEmpty()) Name = TEXT("Unnamed Template");

			FString TemplateId = FString::Printf(TEXT("tpl_%s_%lld"),
				*Name.Replace(TEXT(" "), TEXT("_")), FDateTime::Now().ToUnixTimestamp());

			TSharedPtr<FJsonObject> TplObj = MakeShareable(new FJsonObject());
			TplObj->SetStringField(TEXT("id"),          TemplateId);
			TplObj->SetStringField(TEXT("name"),        Name);
			TplObj->SetStringField(TEXT("description"), Description.IsEmpty() ? Name : Description);
			TplObj->SetStringField(TEXT("node_graph"),  CapturedT3D);
			TplObj->SetNumberField(TEXT("timestamp"),   (double)FDateTime::Now().ToUnixTimestamp());
			TplObj->SetNumberField(TEXT("node_count"),  CapturedCount);
			TplObj->SetStringField(TEXT("category"),    Category.IsEmpty() ? TEXT("General") : Category);
			TplObj->SetStringField(TEXT("tags"),        TEXT(""));

			FString PostBody;
			TSharedRef<TJsonWriter<>> PW = TJsonWriterFactory<>::Create(&PostBody);
			FJsonSerializer::Serialize(TplObj.ToSharedRef(), PW);

			FString SupabaseURL = GetTemplateRegistryBase();
			FString AnonKey     = GetTemplateRegistryToken();

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SaveReq = FHttpModule::Get().CreateRequest();
			SaveReq->SetURL(SupabaseURL + TEXT("/rest/v1/blueprint_templates"));
			SaveReq->SetVerb(TEXT("POST"));
			SaveReq->SetHeader(TEXT("apikey"), AnonKey);
			SaveReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
			SaveReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			SaveReq->SetHeader(TEXT("Prefer"), TEXT("return=representation"));
			SaveReq->SetContentAsString(PostBody);

			SaveReq->OnProcessRequestComplete().BindLambda(
				[Name, Category, CapturedCount, ShowNote](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK2)
			{
				if (bOK2 && Resp.IsValid() && (Resp->GetResponseCode() == 200 || Resp->GetResponseCode() == 201))
				{
					ShowNote(FString::Printf(TEXT("Template saved: \"%s\" (%s, %d nodes)"), *Name, *Category, CapturedCount));
				}
				else
				{
					FString Detail = (Resp.IsValid()) ? FString::Printf(TEXT("HTTP %d: %s"), Resp->GetResponseCode(), *Resp->GetContentAsString().Left(300)) : TEXT("No response");
					UE_LOG(LogUECPShell, Error, TEXT("Template Export POST failed: %s"), *Detail);
					ShowNote(FString::Printf(TEXT("Template Export: Failed to save to server. %s"), *Detail), true);
				}
			});
			SaveReq->ProcessRequest();
		});
	});

	Request->ProcessRequest();
	return FReply::Handled();
}

bool SUECPMainWidget::TryDispatchTemplatesTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("search_templates"))  { OutResult = ExecuteTool_SearchBlueprintTemplates(Arguments);  return true; }
	if (ToolName == TEXT("apply_template"))    { OutResult = ExecuteTool_ApplyBlueprintTemplate(Arguments);    return true; }
	if (ToolName == TEXT("export_template"))   { OutResult = ExecuteTool_ExportBlueprintTemplate(Arguments);   return true; }
	return false;
}

#undef LOCTEXT_NAMESPACE
