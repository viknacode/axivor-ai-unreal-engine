// Axivor AI — Unreal 5.8 native Toolset Registry + File Sandbox bridge.
//
// Exposes, through the normal Axivor tool dispatcher, everything registered in
// the engine's Toolset Registry (UE 5.8, Experimental). This is the exact tool
// layer Epic's built-in "Unreal MCP" server serves over HTTP — here it is called
// in-process, so it works without starting that server and is gated by the same
// interaction modes / confirmations as every other Axivor tool.
//
// Umbrella: `engine`
//   engine_list_toolsets     — catalogue of toolsets (name, description, tools)
//   engine_describe_toolset  — JSON schema (params) of one toolset's tools
//   engine_call_tool         — run "Toolset.Tool" with a JSON input object
//   engine_call_tool_result  — poll a long-running call by job_id
//   engine_sandbox           — File Sandbox: status / enter / changes / persist / discard / leave
//   engine_mcp_server        — start / status of the editor's native MCP HTTP server

#include "UECPEngineToolsetsExtModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"
#include "Managers/SettingsManager.h"

#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/Toolset.h"
#include "ToolsetRegistry/SandboxLibrary.h"
#include "Types/SandboxedFileChangeInfo.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Async/Future.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "Editor.h"

DEFINE_LOG_CATEGORY(LogUECPEngineToolsetsExt);

namespace
{
	using namespace UE::ToolsetRegistry;

	// ── JSON helpers ────────────────────────────────────────────────────────
	static FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	static FString ArgStr(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Default = FString())
	{
		FString V;
		if (Args.IsValid() && Args->TryGetStringField(Key, V)) return V;
		return Default;
	}

	static FUECPToolResult Fail(const FString& Msg)
	{
		FUECPToolResult R;
		R.bSuccess = false;
		R.ErrorMessage = Msg;
		return R;
	}

	static FUECPToolResult Ok(const TSharedRef<FJsonObject>& Obj)
	{
		FUECPToolResult R;
		R.bSuccess = true;
		R.ResultJson = ToJson(Obj);
		return R;
	}

	static FUECPToolResult OkRaw(const FString& Json)
	{
		FUECPToolResult R;
		R.bSuccess = true;
		R.ResultJson = Json;
		return R;
	}

	static UToolsetRegistrySubsystem* GetRegistry(FString& OutErr)
	{
		if (!GEditor)
		{
			OutErr = TEXT("Editor not available.");
			return nullptr;
		}
		TValueOrError<TObjectPtr<UToolsetRegistrySubsystem>, FString> R = UToolsetRegistrySubsystem::Get();
		if (R.HasError())
		{
			OutErr = R.GetError().IsEmpty()
				? TEXT("Toolset Registry subsystem unavailable — enable the 'Toolset Registry' and 'All Toolsets' plugins and restart the editor.")
				: R.GetError();
			return nullptr;
		}
		return R.GetValue().Get();
	}

	// ── Pending long-running tool calls ─────────────────────────────────────
	struct FPendingCall
	{
		FString ToolName;
		double StartedAt = 0.0;
		TFuture<TValueOrError<FString, FString>> Future;
	};
	static TMap<FString, TSharedPtr<FPendingCall>> GPendingCalls;

	static void AppendResult(const TSharedRef<FJsonObject>& Out, TValueOrError<FString, FString>& Res)
	{
		if (Res.HasError())
		{
			Out->SetBoolField(TEXT("ok"), false);
			Out->SetStringField(TEXT("error"), Res.GetError());
			return;
		}
		Out->SetBoolField(TEXT("ok"), true);
		const FString& Raw = Res.GetValue();
		// Tools usually return JSON; pass it through as an object when it parses, else as text.
		TSharedPtr<FJsonValue> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!Raw.IsEmpty() && FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			Out->SetField(TEXT("result"), Parsed);
		else
			Out->SetStringField(TEXT("result"), Raw);
	}

	// ── Handlers ─────────────────────────────────────────────────────────────
	static FUECPToolResult HandleListToolsets(const TSharedPtr<FJsonObject>& Args)
	{
		FString Err;
		UToolsetRegistrySubsystem* Sub = GetRegistry(Err);
		if (!Sub) return Fail(Err);

		const FString Filter = ArgStr(Args, TEXT("filter")).ToLower();
		const bool bIncludeTools = !Args.IsValid() || !Args->HasField(TEXT("include_tools")) || Args->GetBoolField(TEXT("include_tools"));

		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 ToolTotal = 0;
		Sub->ToolsetRegistry.ForEachToolset([&](const FString& Name, const FToolset& T)
		{
			const FString Desc = T.GetToolsetDescription();
			if (!Filter.IsEmpty() && !Name.ToLower().Contains(Filter) && !Desc.ToLower().Contains(Filter)) return;
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Name);
			O->SetStringField(TEXT("description"), Desc);
			O->SetStringField(TEXT("version"), T.GetToolsetVersion());
			O->SetBoolField(TEXT("enabled"), T.IsEnabled());
			const TArray<FString> Tools = T.ListToolNames();
			ToolTotal += Tools.Num();
			O->SetNumberField(TEXT("tool_count"), Tools.Num());
			if (bIncludeTools)
			{
				TArray<TSharedPtr<FJsonValue>> TArr;
				for (const FString& Tool : Tools) TArr.Add(MakeShared<FJsonValueString>(Tool));
				O->SetArrayField(TEXT("tools"), TArr);
			}
			Arr.Add(MakeShared<FJsonValueObject>(O));
		});

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("toolset_count"), Arr.Num());
		Out->SetNumberField(TEXT("tool_count"), ToolTotal);
		Out->SetArrayField(TEXT("toolsets"), Arr);
		Out->SetStringField(TEXT("hint"), TEXT("Call engine_describe_toolset(toolset) for parameter schemas, then engine_call_tool(toolset, tool, input)."));
		return Ok(Out);
	}

	static FUECPToolResult HandleDescribeToolset(const TSharedPtr<FJsonObject>& Args)
	{
		FString Err;
		UToolsetRegistrySubsystem* Sub = GetRegistry(Err);
		if (!Sub) return Fail(Err);

		const FString Name = ArgStr(Args, TEXT("toolset"));
		if (Name.IsEmpty()) return Fail(TEXT("Missing 'toolset'. Use engine_list_toolsets to see the names."));

		FString FindErr;
		TSharedPtr<FToolset> T = Sub->ToolsetRegistry.Find(Name, false, &FindErr);
		if (!T.IsValid()) return Fail(FindErr.IsEmpty() ? FString::Printf(TEXT("Toolset '%s' not found."), *Name) : FindErr);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), T->GetToolsetName());
		Out->SetStringField(TEXT("description"), T->GetToolsetDescription());
		Out->SetBoolField(TEXT("enabled"), T->IsEnabled());

		const FString Schema = T->GetJsonSchema();
		TSharedPtr<FJsonValue> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Schema);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			Out->SetField(TEXT("schema"), Parsed);
		else
			Out->SetStringField(TEXT("schema"), Schema);
		Out->SetStringField(TEXT("hint"), TEXT("Tool names are '<Toolset>.<Tool>'. Pass parameters as the 'input' object of engine_call_tool."));
		return Ok(Out);
	}

	static FUECPToolResult HandleCallTool(const TSharedPtr<FJsonObject>& Args)
	{
		FString Err;
		UToolsetRegistrySubsystem* Sub = GetRegistry(Err);
		if (!Sub) return Fail(Err);

		FString Toolset = ArgStr(Args, TEXT("toolset"));
		FString Tool    = ArgStr(Args, TEXT("tool"));
		const FString Full = ArgStr(Args, TEXT("name"));
		if (!Full.IsEmpty() && (Toolset.IsEmpty() || Tool.IsEmpty()))
		{
			int32 Dot = INDEX_NONE;
			if (Full.FindChar(TEXT('.'), Dot)) { Toolset = Full.Left(Dot); Tool = Full.Mid(Dot + 1); }
		}
		if (Toolset.IsEmpty() || Tool.IsEmpty())
			return Fail(TEXT("Provide 'toolset' + 'tool' (or name='Toolset.Tool'). See engine_list_toolsets."));

		// Input: object → serialised; string → used as-is; missing → {}
		FString JsonInput = TEXT("{}");
		if (Args.IsValid() && Args->HasField(TEXT("input")))
		{
			const TSharedPtr<FJsonObject>* InObj = nullptr;
			FString InStr;
			if (Args->TryGetObjectField(TEXT("input"), InObj) && InObj && InObj->IsValid())
				JsonInput = ToJson(InObj->ToSharedRef());
			else if (Args->TryGetStringField(TEXT("input"), InStr) && !InStr.IsEmpty())
				JsonInput = InStr;
		}

		double WaitMs = 3000.0;
		if (Args.IsValid()) Args->TryGetNumberField(TEXT("wait_ms"), WaitMs);
		WaitMs = FMath::Clamp(WaitMs, 0.0, 30000.0);

		FToolDescriptor Desc;
		Desc.ToolsetName = Toolset;
		Desc.ToolName    = Tool;

		const double T0 = FPlatformTime::Seconds();
		TFuture<TValueOrError<FString, FString>> Future = Sub->ToolsetRegistry.ExecuteTool(Desc, JsonInput);

		// Most toolset calls complete synchronously on the game thread. Give async
		// ones a short grace period, then hand back a job id to poll.
		while (!Future.IsReady() && (FPlatformTime::Seconds() - T0) * 1000.0 < WaitMs)
			FPlatformProcess::SleepNoStats(0.005f);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("tool"), Toolset + TEXT(".") + Tool);
		if (Future.IsReady())
		{
			TValueOrError<FString, FString> Res = Future.Get();
			AppendResult(Out, Res);
			Out->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
			return Ok(Out);
		}

		TSharedPtr<FPendingCall> Pending = MakeShared<FPendingCall>();
		Pending->ToolName  = Toolset + TEXT(".") + Tool;
		Pending->StartedAt = T0;
		Pending->Future    = MoveTemp(Future);
		const FString JobId = FGuid::NewGuid().ToString(EGuidFormats::Short);
		GPendingCalls.Add(JobId, Pending);

		Out->SetBoolField(TEXT("ok"), true);
		Out->SetStringField(TEXT("status"), TEXT("pending"));
		Out->SetStringField(TEXT("job_id"), JobId);
		Out->SetStringField(TEXT("hint"), TEXT("Long-running tool. Poll engine_call_tool_result(job_id) until status is 'done'."));
		return Ok(Out);
	}

	static FUECPToolResult HandleCallToolResult(const TSharedPtr<FJsonObject>& Args)
	{
		const FString JobId = ArgStr(Args, TEXT("job_id"));
		TSharedPtr<FPendingCall>* Found = GPendingCalls.Find(JobId);
		if (!Found || !Found->IsValid()) return Fail(FString::Printf(TEXT("Unknown job_id '%s'."), *JobId));

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("tool"), (*Found)->ToolName);
		Out->SetStringField(TEXT("job_id"), JobId);
		if (!(*Found)->Future.IsReady())
		{
			Out->SetBoolField(TEXT("ok"), true);
			Out->SetStringField(TEXT("status"), TEXT("pending"));
			Out->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - (*Found)->StartedAt) * 1000.0);
			return Ok(Out);
		}
		TValueOrError<FString, FString> Res = (*Found)->Future.Get();
		AppendResult(Out, Res);
		Out->SetStringField(TEXT("status"), TEXT("done"));
		GPendingCalls.Remove(JobId);
		return Ok(Out);
	}

	static const TCHAR* ChangeToString(UE::FileSandboxCore::ESandboxFileChange C)
	{
		switch (C)
		{
		case UE::FileSandboxCore::ESandboxFileChange::Added:   return TEXT("added");
		case UE::FileSandboxCore::ESandboxFileChange::Removed: return TEXT("removed");
		case UE::FileSandboxCore::ESandboxFileChange::Edited:  return TEXT("edited");
		default:                                               return TEXT("none");
		}
	}

	static TSharedRef<FJsonObject> SandboxStatus()
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		const bool bActive = FGlobalSandbox::IsActive();
		Out->SetBoolField(TEXT("active"), bActive);
		Out->SetStringField(TEXT("name"), bActive ? FGlobalSandbox::GetActiveName() : FString());
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (bActive)
		{
			for (const UE::FileSandboxCore::FSandboxedFileChangeInfo& C : FGlobalSandbox::GetChanges())
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("path"), C.Path);
				O->SetStringField(TEXT("change"), ChangeToString(C.Action));
				O->SetStringField(TEXT("timestamp"), C.Timestamp.ToIso8601());
				Arr.Add(MakeShared<FJsonValueObject>(O));
			}
		}
		Out->SetNumberField(TEXT("change_count"), Arr.Num());
		Out->SetArrayField(TEXT("changes"), Arr);
		return Out;
	}

	static FUECPToolResult HandleSandbox(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Action = ArgStr(Args, TEXT("action"), TEXT("status")).ToLower();
		TArray<FString> Files;
		if (Args.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* FArr = nullptr;
			if (Args->TryGetArrayField(TEXT("files"), FArr) && FArr)
				for (const TSharedPtr<FJsonValue>& V : *FArr) { FString S; if (V.IsValid() && V->TryGetString(S)) Files.Add(S); }
		}

		if (Action == TEXT("status") || Action == TEXT("changes"))
		{
			return Ok(SandboxStatus());
		}
		if (Action == TEXT("enter"))
		{
			const FString Name = ArgStr(Args, TEXT("name"), TEXT("AxivorAI"));
			const FString Desc = ArgStr(Args, TEXT("description"), TEXT("Axivor AI sandboxed edits"));
			if (!FGlobalSandbox::Enter(Name, Desc)) return Fail(TEXT("Could not enter the file sandbox (is the File Sandbox plugin enabled?)."));
			TSharedRef<FJsonObject> Out = SandboxStatus();
			Out->SetStringField(TEXT("message"), TEXT("Sandbox active: asset/file writes are captured until persist or discard."));
			return Ok(Out);
		}
		if (!FGlobalSandbox::IsActive()) return Fail(TEXT("No sandbox is active. Call engine_sandbox(action='enter') first."));
		if (Action == TEXT("persist") || Action == TEXT("commit") || Action == TEXT("apply"))
		{
			if (Files.Num() == 0)
				for (const UE::FileSandboxCore::FSandboxedFileChangeInfo& C : FGlobalSandbox::GetChanges()) Files.Add(C.Path);
			const bool bOk = FGlobalSandbox::Persist(Files);
			TSharedRef<FJsonObject> Out = SandboxStatus();
			Out->SetBoolField(TEXT("persisted"), bOk);
			Out->SetNumberField(TEXT("persisted_count"), bOk ? Files.Num() : 0);
			return bOk ? Ok(Out) : Fail(TEXT("Persist failed for one or more files."));
		}
		if (Action == TEXT("discard") || Action == TEXT("revert"))
		{
			const bool bOk = Files.Num() ? FGlobalSandbox::DiscardFiles(Files) : FGlobalSandbox::Discard();
			TSharedRef<FJsonObject> Out = SandboxStatus();
			Out->SetBoolField(TEXT("discarded"), bOk);
			return bOk ? Ok(Out) : Fail(TEXT("Discard failed."));
		}
		if (Action == TEXT("leave") || Action == TEXT("exit"))
		{
			const bool bOk = FGlobalSandbox::Leave();
			TSharedRef<FJsonObject> Out = SandboxStatus();
			Out->SetBoolField(TEXT("left"), bOk);
			return bOk ? Ok(Out) : Fail(TEXT("Leave failed."));
		}
		return Fail(TEXT("Unknown action. Use: status | enter | changes | persist | discard | leave."));
	}

	static FUECPToolResult HandleMcpServer(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Action = ArgStr(Args, TEXT("action"), TEXT("status")).ToLower();
		const bool bLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("ModelContextProtocolEditor"))
			|| FModuleManager::Get().IsModuleLoaded(TEXT("ModelContextProtocol"));
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("plugin_loaded"), bLoaded);
		int32 Port = 8000;
		if (Args.IsValid()) { double D = 0; if (Args->TryGetNumberField(TEXT("port"), D) && D > 0) Port = (int32)D; }
		if (!bLoaded)
		{
			Out->SetStringField(TEXT("message"), TEXT("The 'Unreal MCP' (ModelContextProtocol) plugin is not loaded. Enable it in Edit > Plugins and restart to expose the editor to external agents (Claude Code, Cursor...)."));
			return Ok(Out);
		}
		if (Action == TEXT("start"))
		{
			if (GEditor) GEditor->Exec(nullptr, *FString::Printf(TEXT("ModelContextProtocol.StartServer %d"), Port));
			Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Start requested on http://127.0.0.1:%d/mcp (loopback only). Generate client configs with console: ModelContextProtocol.GenerateClientConfig All"), Port));
			Out->SetStringField(TEXT("url"), FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Port));
			return Ok(Out);
		}
		if (Action == TEXT("generate_client_config"))
		{
			const FString Client = ArgStr(Args, TEXT("client"), TEXT("All"));
			if (GEditor) GEditor->Exec(nullptr, *FString::Printf(TEXT("ModelContextProtocol.GenerateClientConfig %s"), *Client));
			Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Client config generation requested for '%s' (writes .mcp.json in the project root)."), *Client));
			return Ok(Out);
		}
		Out->SetStringField(TEXT("message"), TEXT("Actions: status | start (port) | generate_client_config (client=ClaudeCode|Cursor|VSCode|Gemini|Codex|All)."));
		return Ok(Out);
	}
}

void FUECPEngineToolsetsExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("engine_list_toolsets"),    HandleListToolsets);
	D.RegisterHandler(TEXT("engine_describe_toolset"), HandleDescribeToolset);
	D.RegisterHandler(TEXT("engine_call_tool"),        HandleCallTool);
	D.RegisterHandler(TEXT("engine_call_tool_result"), HandleCallToolResult);
	D.RegisterHandler(TEXT("engine_sandbox"),          HandleSandbox);
	D.RegisterHandler(TEXT("engine_mcp_server"),       HandleMcpServer);

	{
		const FName U(TEXT("engine"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };
		Meta(TEXT("engine_list_toolsets"),    TEXT("List every UE 5.8 native toolset (scene, actor, material, editor, UMG, PCG, Niagara, GAS, physics, Slate, tests, plugins, config, skills...) with their tool names."), TEXT("filter?, include_tools?=true"));
		Meta(TEXT("engine_describe_toolset"), TEXT("JSON schema (parameters + descriptions) of all tools in one native toolset."), TEXT("toolset"));
		Meta(TEXT("engine_call_tool"),        TEXT("Execute a native toolset tool in-process: '<Toolset>.<Tool>' with a JSON input object. Returns the tool result, or a job_id when long-running."), TEXT("toolset, tool | name='Toolset.Tool', input{}, wait_ms?=3000"));
		Meta(TEXT("engine_call_tool_result"), TEXT("Poll a long-running native tool call."), TEXT("job_id"));
		Meta(TEXT("engine_sandbox"),          TEXT("File Sandbox: capture asset/file edits so they can be persisted or discarded as a batch (status | enter | changes | persist | discard | leave)."), TEXT("action, name?, description?, files?[]"));
		Meta(TEXT("engine_mcp_server"),       TEXT("Native Unreal MCP HTTP server (for external agents like Claude Code / Cursor): status | start | generate_client_config."), TEXT("action, port?=8000, client?=All"));
	}

	using UECPToolSafety::RegisterToolSafety;
	RegisterToolSafety(TEXT("engine_list_toolsets"),    EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("engine_describe_toolset"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("engine_call_tool_result"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("engine_call_tool"),        EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("engine_sandbox"),          EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("engine_mcp_server"),       EUECPToolSafety::Write);

	// Turbo + sandbox: capture every write in a sandbox so the user can persist or discard in one go.
	bool bTurbo = false, bTurboSandbox = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"),    bTurbo,        FSettingsManager::GetGlobalConfigPath());
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboSandbox"), bTurboSandbox, FSettingsManager::GetGlobalConfigPath());
	if (bTurbo && bTurboSandbox && !FGlobalSandbox::IsActive())
	{
		const bool bOk = FGlobalSandbox::Enter(TEXT("AxivorAI"), TEXT("Axivor AI turbo-mode sandbox"));
		UE_LOG(LogUECPEngineToolsetsExt, Log, TEXT("Turbo sandbox %s"), bOk ? TEXT("entered") : TEXT("could not be entered"));
	}

	UE_LOG(LogUECPEngineToolsetsExt, Log, TEXT("Axivor engine toolsets bridge registered (umbrella 'engine')."));
}

void FUECPEngineToolsetsExtModule::ShutdownModule()
{
	GPendingCalls.Empty();
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const TCHAR* Name : { TEXT("engine_list_toolsets"), TEXT("engine_describe_toolset"), TEXT("engine_call_tool"),
	                           TEXT("engine_call_tool_result"), TEXT("engine_sandbox"), TEXT("engine_mcp_server") })
	{
		D.UnregisterHandler(FName(Name));
	}
}

IMPLEMENT_MODULE(FUECPEngineToolsetsExtModule, UECPEngineToolsetsExt)
