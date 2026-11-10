// Copyright 2026, BlueprintsLab, All rights reserved

#include "AxivorEngineToolset.h"

#include "../Protocol/McpToolsCatalog.h"
#include "UECPMCPBridgeModule.h"

#include "Managers/SettingsManager.h"

#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/ToolAccessMode.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using namespace UE::ToolsetRegistry;

const TCHAR* FAxivorEngineToolset::ToolsetName = TEXT("Axivor");

namespace
{
	const TCHAR* SearchToolsName = TEXT("SearchTools");
	const TCHAR* GetToolDocsName = TEXT("GetToolDocs");
	const TCHAR* CallToolName    = TEXT("CallTool");

	TSharedPtr<FAxivorEngineToolset> GToolset;
	FDelegateHandle GModulesChangedHandle;
	FDelegateHandle GInstructionsHandle;

	using FToolResult = TValueOrError<FString, FString>;

	TFuture<FToolResult> Answer(FToolResult&& Result)
	{
		return MakeFulfilledPromise<FToolResult>(MoveTemp(Result)).GetFuture();
	}

	FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	TSharedRef<FJsonObject> StringProp(const TCHAR* Description)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("string"));
		P->SetStringField(TEXT("description"), Description);
		return P;
	}

	// One tool entry, with every field a reflected toolset carries (name, description,
	// inputSchema, outputSchema, annotations.accessMode). The registry consumers validate
	// the whole schema set and reject it when a field is missing.
	TSharedRef<FJsonValue> DescribeTool(const FString& FullName, const TCHAR* Description,
		const TSharedRef<FJsonObject>& Properties, const TArray<FString>& Required, EToolAccessMode Mode)
	{
		TArray<TSharedPtr<FJsonValue>> Req;
		for (const FString& R : Required) Req.Add(MakeShared<FJsonValueString>(R));

		TSharedRef<FJsonObject> Input = MakeShared<FJsonObject>();
		Input->SetStringField(TEXT("type"), TEXT("object"));
		Input->SetObjectField(TEXT("properties"), Properties);
		Input->SetArrayField(TEXT("required"), Req);

		TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
		Output->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetStringField(TEXT("accessMode"), ToAccessModeString(Mode));

		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), FullName);
		Tool->SetStringField(TEXT("description"), Description);
		Tool->SetObjectField(TEXT("inputSchema"), Input);
		Tool->SetObjectField(TEXT("outputSchema"), Output);
		Tool->SetObjectField(TEXT("annotations"), Annotations);
		return MakeShared<FJsonValueObject>(Tool);
	}

	// Runs one Axivor tool through the MCP request pipeline (modes, crew, confirmations)
	// and maps the MCP tools/call result onto the registry's value-or-error.
	FToolResult RunThroughPipeline(const FString& DispatchName, const TSharedPtr<FJsonObject>& Arguments)
	{
		TSharedRef<FJsonObject> CallParams = MakeShared<FJsonObject>();
		CallParams->SetStringField(TEXT("name"), DispatchName);
		CallParams->SetObjectField(TEXT("arguments"), Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());

		FMcpInvocationContext Ctx;
		Ctx.IsTransportAlive = [] { return true; };

		const TSharedRef<FJsonObject> Result = McpToolsCatalog::HandleToolsCall(CallParams, Ctx);

		FString Text;
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Result->TryGetArrayField(TEXT("content"), Content) && Content)
		{
			for (const TSharedPtr<FJsonValue>& V : *Content)
			{
				const TSharedPtr<FJsonObject>* Block = nullptr;
				FString Piece;
				if (V.IsValid() && V->TryGetObject(Block) && Block && (*Block)->TryGetStringField(TEXT("text"), Piece))
					Text += Piece;
			}
		}
		if (Text.IsEmpty())
		{
			// Protocol-level error (bad params): HandleToolsCall answers with {error:{message}}.
			const TSharedPtr<FJsonObject>* Err = nullptr;
			FString Msg;
			if (Result->TryGetObjectField(TEXT("error"), Err) && Err && (*Err)->TryGetStringField(TEXT("message"), Msg))
				return MakeError(Msg);
			Text = ToJson(Result);
		}

		bool bIsError = false;
		Result->TryGetBoolField(TEXT("isError"), bIsError);
		if (bIsError) return MakeError(Text);

		// Tools answer with JSON objects; wrap anything else so the result is always an object.
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			return MakeValue(Text);
		TSharedRef<FJsonObject> Wrapped = MakeShared<FJsonObject>();
		Wrapped->SetStringField(TEXT("result"), Text);
		return MakeValue(ToJson(Wrapped));
	}

	FToolResult ExecuteOnGameThread(const FString& ToolName, const FString& JsonInput)
	{
		TSharedPtr<FJsonObject> Input;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonInput.IsEmpty() ? FString(TEXT("{}")) : JsonInput);
			if (!FJsonSerializer::Deserialize(Reader, Input) || !Input.IsValid())
				return MakeError(TEXT("Input must be a JSON object."));
		}

		if (ToolName == SearchToolsName)
		{
			return RunThroughPipeline(TEXT("search_tools"), Input);
		}
		if (ToolName == GetToolDocsName)
		{
			return RunThroughPipeline(TEXT("get_tool_docs"), Input);
		}
		if (ToolName == CallToolName)
		{
			FString Tool;
			if (!Input->TryGetStringField(TEXT("tool"), Tool) || Tool.IsEmpty())
				return MakeError(TEXT("Missing 'tool'. Use Axivor.SearchTools to find the action name, then pass it as 'tool' (or the umbrella as 'tool' plus arguments.action)."));

			TSharedPtr<FJsonObject> Args;
			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			FString ArgsStr;
			if (Input->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
			{
				Args = *ArgsObj;
			}
			else if (Input->TryGetStringField(TEXT("arguments"), ArgsStr) && !ArgsStr.IsEmpty())
			{
				const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ArgsStr);
				if (!FJsonSerializer::Deserialize(R, Args) || !Args.IsValid())
					return MakeError(TEXT("'arguments' must be a JSON object."));
			}
			if (!Args.IsValid()) Args = MakeShared<FJsonObject>();
			return RunThroughPipeline(Tool, Args);
		}
		return MakeError(FString::Printf(TEXT("Unknown tool '%s' in toolset 'Axivor'."), *ToolName));
	}

	bool IsExposureEnabled()
	{
		bool bEnabled = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ExposeToEngineToolsetRegistry"), bEnabled,
			FSettingsManager::GetGlobalConfigPath());
		return bEnabled;
	}

	void TryRegisterNow()
	{
		if (GToolset.IsValid()) return;
		FToolsetRegistry* Registry = FToolsetRegistry::Get();
		if (!Registry) return;

		TSharedPtr<FAxivorEngineToolset> Toolset = MakeShared<FAxivorEngineToolset>();
		if (!Registry->RegisterToolset(Toolset))
		{
			UE_LOG(LogUECPMCPBridge, Warning, TEXT("Axivor: could not register the 'Axivor' toolset in the engine Toolset Registry (name taken?)."));
			return;
		}
		GToolset = Toolset;

		GInstructionsHandle = Registry->OnGatherInstructions().AddLambda([](TArray<FString>& Out)
		{
			Out.Add(TEXT(
				"Axivor AI tools are available through the 'Axivor' toolset: call Axivor.SearchTools(query='<english intent>') "
				"to find an action (it returns the action, its umbrella and params), Axivor.GetToolDocs(category, action?) for full "
				"parameters, then Axivor.CallTool(tool='<action>', arguments={...}). Axivor covers Blueprints, materials, Niagara, "
				"animation, Control Rig, StateTree, PCG, GAS, UI, levels and project scanning, and respects the user's Axivor "
				"interaction mode (Just Chat / Plan / Auto Edit)."));
		});

		if (GModulesChangedHandle.IsValid())
		{
			FModuleManager::Get().OnModulesChanged().Remove(GModulesChangedHandle);
			GModulesChangedHandle.Reset();
		}
		UE_LOG(LogUECPMCPBridge, Log, TEXT("Axivor: registered toolset 'Axivor' in the engine Toolset Registry (visible to Unreal MCP and Unreal Assistant)."));
	}
}

FString FAxivorEngineToolset::GetToolsetName() const
{
	return ToolsetName;
}

FString FAxivorEngineToolset::GetToolsetVersion() const
{
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")))
		return Plugin->GetDescriptor().VersionName;
	return TEXT("1.0");
}

FString FAxivorEngineToolset::GetToolsetDescription() const
{
	return TEXT("Axivor AI engineering copilot: hundreds of editor actions (Blueprint graph authoring, materials, Niagara, "
		"animation/Control Rig/PoseSearch, StateTree, Behavior Trees, PCG, GAS, UMG, levels, project scanning, crew agents). "
		"Use SearchTools to find an action by intent, GetToolDocs for its parameters, then CallTool to run it.");
}

TFuture<TValueOrError<FString, FString>> FAxivorEngineToolset::ExecuteToolInternal(
	const FString& ToolName, const FString& JsonInput, FToolCallContext Context)
{
	if (IsInGameThread())
	{
		return Answer(ExecuteOnGameThread(ToolName, JsonInput));
	}

	// Axivor tools touch UObjects: run them on the game thread, from the core ticker so they never
	// execute re-entrantly inside the world tick (see UECPToolDispatcher.cpp for the Mass assert).
	TSharedRef<TPromise<FToolResult>, ESPMode::ThreadSafe> Promise = MakeShared<TPromise<FToolResult>, ESPMode::ThreadSafe>();
	TFuture<FToolResult> Future = Promise->GetFuture();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Promise, ToolName, JsonInput](float) -> bool
	{
		Promise->SetValue(ExecuteOnGameThread(ToolName, JsonInput));
		return false;
	}));
	return Future;
}

FToolsetJsonSchema FAxivorEngineToolset::GetJsonSchemaInternal() const
{
	const FString Prefix = GetToolsetName() + TEXT(".");
	TArray<TSharedPtr<FJsonValue>> Tools;

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("query"), StringProp(TEXT("English intent keywords, e.g. 'set material two sided' or 'add IK to control rig'. Query in English even when the chat is in another language.")));
		P->SetObjectField(TEXT("umbrella"), StringProp(TEXT("Optional category; with no query, lists every action in that umbrella.")));
		Tools.Add(DescribeTool(Prefix + SearchToolsName,
			TEXT("Find the right Axivor action by intent. Returns matching actions with their umbrella and parameters — usually enough to call Axivor.CallTool directly."),
			P, {}, EToolAccessMode::Read));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("category"), StringProp(TEXT("Umbrella / category name returned by SearchTools.")));
		P->SetObjectField(TEXT("action"), StringProp(TEXT("Optional: only this action's parameters (token-cheap).")));
		Tools.Add(DescribeTool(Prefix + GetToolDocsName,
			TEXT("Parameter documentation, recipes and gotchas for an Axivor umbrella, or for a single action."),
			P, { TEXT("category") }, EToolAccessMode::Read));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("tool"), StringProp(TEXT("Action name from SearchTools (e.g. 'create_blueprint'), or an umbrella name with arguments.action set.")));
		TSharedRef<FJsonObject> ArgsProp = MakeShared<FJsonObject>();
		ArgsProp->SetStringField(TEXT("type"), TEXT("object"));
		ArgsProp->SetStringField(TEXT("description"), TEXT("The action's parameters as a JSON object."));
		P->SetObjectField(TEXT("arguments"), ArgsProp);
		Tools.Add(DescribeTool(Prefix + CallToolName,
			TEXT("Run an Axivor action. Goes through Axivor's safety pipeline: the user's interaction mode (Just Chat / Plan / Auto Edit), crew permissions and destructive-operation confirmations apply."),
			P, { TEXT("tool") }, EToolAccessMode::Execute));
	}

	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("name"), GetToolsetName());
	Schema->SetStringField(TEXT("version"), GetToolsetVersion());
	Schema->SetStringField(TEXT("description"), GetToolsetDescription());
	Schema->SetArrayField(TEXT("tools"), Tools);
	return FToolsetJsonSchema(Schema);
}

namespace UECPEngineToolset
{
	void Register()
	{
		if (!IsExposureEnabled())
		{
			UE_LOG(LogUECPMCPBridge, Log, TEXT("Axivor: engine Toolset Registry exposure disabled (ExposeToEngineToolsetRegistry=False)."));
			return;
		}
		TryRegisterNow();
		if (!GToolset.IsValid() && !GModulesChangedHandle.IsValid())
		{
			GModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddLambda(
				[](FName ModuleName, EModuleChangeReason Reason)
				{
					if (Reason == EModuleChangeReason::ModuleLoaded && ModuleName == TEXT("ToolsetRegistry"))
						TryRegisterNow();
				});
		}
	}

	void Unregister()
	{
		if (GModulesChangedHandle.IsValid())
		{
			FModuleManager::Get().OnModulesChanged().Remove(GModulesChangedHandle);
			GModulesChangedHandle.Reset();
		}
		if (FToolsetRegistry* Registry = FToolsetRegistry::Get())
		{
			if (GInstructionsHandle.IsValid()) Registry->OnGatherInstructions().Remove(GInstructionsHandle);
			if (GToolset.IsValid()) Registry->UnregisterToolset(GToolset);
		}
		GInstructionsHandle.Reset();
		GToolset.Reset();
	}

	bool IsRegistered()
	{
		return GToolset.IsValid();
	}
}
