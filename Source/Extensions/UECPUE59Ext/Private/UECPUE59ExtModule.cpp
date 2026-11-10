// Axivor AI — Unreal Engine 5.9 agentic feature pack (umbrella `ue59`).
//
// 5.9 turns the editor into an agent host: the Unreal MCP server starts with the editor,
// dozens of new Toolset Registry toolsets ship (Blueprint Graph DSL, Console, Perf,
// NavMesh, FileSystem, SceneCapture, Knowledge Base, MCP Client...), Markdown Agent
// Skills are discovered from plugins/projects, and Unreal Assistant runs external
// agents in a panel. This pack reports that stack, enables it in one call and gives the
// Axivor agent access to the same Agent Skills. Plugin classes are resolved by
// reflection, so nothing here hard-links experimental plugins beyond ToolsetRegistry.

#include "UECPUE59ExtModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"

#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/MarkdownAgentSkills.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

DEFINE_LOG_CATEGORY(LogUECPUE59Ext);

namespace
{
	using namespace UE::ToolsetRegistry;

	static FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}
	static FString ArgStr(const TSharedPtr<FJsonObject>& A, const TCHAR* K, const FString& D = FString())
	{
		FString V; if (A.IsValid() && A->TryGetStringField(K, V)) return V; return D;
	}
	static bool ArgBool(const TSharedPtr<FJsonObject>& A, const TCHAR* K, bool D)
	{
		bool V = D; if (A.IsValid() && A->HasTypedField<EJson::Boolean>(K)) V = A->GetBoolField(K); return V;
	}
	static FUECPToolResult Fail(const FString& Msg) { FUECPToolResult R; R.bSuccess = false; R.ErrorMessage = Msg; return R; }
	static FUECPToolResult Ok(const TSharedRef<FJsonObject>& O) { FUECPToolResult R; R.bSuccess = true; R.ResultJson = ToJson(O); return R; }

	static bool IsPluginEnabled(const FString& Name)
	{
		const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
		return P.IsValid() && P->IsEnabled();
	}

	// ── Plugin groups of the 5.9 agentic stack ──────────────────────────────
	struct FPreset
	{
		const TCHAR* Name;
		const TCHAR* Description;
		TArray<const TCHAR*> Plugins;
	};

	static const TArray<FPreset>& Presets()
	{
		static const TArray<FPreset> P = {
			{ TEXT("agents"),
			  TEXT("Core agent host: native Unreal MCP server, Toolset Registry + every engine toolset, File Sandbox, Agent Skills, Knowledge Base, Claude/Codex agent-plugin discovery."),
			  { TEXT("ModelContextProtocol"), TEXT("ToolsetRegistry"), TEXT("AllToolsets"), TEXT("FileSandbox"),
			    TEXT("KnowledgeBase"), TEXT("KnowledgeBaseToolset"), TEXT("AgentCapability"), TEXT("AgentPluginToolset") } },
			{ TEXT("blueprint_dsl"),
			  TEXT("Blueprint Graph DSL (S-expression read/write of whole graphs) for agents."),
			  { TEXT("ToolsetRegistry"), TEXT("GraphDsl"), TEXT("BlueprintToolsets") } },
			{ TEXT("debug_perf"),
			  TEXT("Console/cvars, cheats, performance triage, Rewind Debugger, Slate inspection, scene capture, navmesh queries."),
			  { TEXT("ToolsetRegistry"), TEXT("ConsoleToolset"), TEXT("CheatsToolset"), TEXT("PerfToolset"), TEXT("RewindDebuggerToolset"),
			    TEXT("SlateDebugger"), TEXT("WidgetFinder"), TEXT("SceneCaptureToolset"), TEXT("MediaToolset"), TEXT("NavMeshToolset") } },
			{ TEXT("ui"),
			  TEXT("UMG/CommonUI agents: widget animation, static analysis, CommonUI and input toolsets."),
			  { TEXT("ToolsetRegistry"), TEXT("WidgetAnimationToolset"), TEXT("WidgetStaticAnalysisToolset"), TEXT("CommonUIToolset"), TEXT("InputToolset") } },
			{ TEXT("canvas"),
			  TEXT("Unreal Canvas (experimental): shared human+agent workspace for image/video/mesh generation, Movie Render Graph as MCP tools, spatial annotations."),
			  { TEXT("ModelContextProtocol"), TEXT("UnrealCanvasBridge"), TEXT("UnrealCanvasGraph"), TEXT("UnrealCanvasAnnotations") } },
			{ TEXT("animgen"),
			  TEXT("AI animation generation: AnimGen for UAF, offline generated animations in Sequencer, Kimodo model."),
			  { TEXT("UAFAnimGen"), TEXT("AnimGenOffline"), TEXT("Kimodo") } },
			{ TEXT("metahuman_agents"),
			  TEXT("MetaHuman toolsets (Animator, calibration, depth generation, Capture Manager)."),
			  { TEXT("ToolsetRegistry"), TEXT("AllMetaHumanToolsets") } },
		};
		return P;
	}

	static const FPreset* FindPreset(const FString& Name)
	{
		for (const FPreset& P : Presets()) if (Name.Equals(P.Name, ESearchCase::IgnoreCase)) return &P;
		return nullptr;
	}

	// Dumps every UPROPERTY of a settings CDO resolved by path (no link dependency).
	static TSharedPtr<FJsonObject> DumpSettings(const TCHAR* ClassPath)
	{
		UClass* Cls = FindObject<UClass>(nullptr, ClassPath);
		if (!Cls) return nullptr;
		const UObject* CDO = Cls->GetDefaultObject();
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Cls, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FString Value;
			It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(CDO), nullptr, nullptr, PPF_None);
			O->SetStringField(It->GetName(), Value);
		}
		return O;
	}

	static bool IsAxivorToolsetRegistered()
	{
		FToolsetRegistry* Reg = FToolsetRegistry::Get();
		return Reg && Reg->Find(TEXT("Axivor")).IsValid();
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue59_feature_status
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleFeatureStatus(const TSharedPtr<FJsonObject>&)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

		TSharedRef<FJsonObject> Groups = MakeShared<FJsonObject>();
		for (const FPreset& P : Presets())
		{
			TSharedRef<FJsonObject> G = MakeShared<FJsonObject>();
			int32 On = 0;
			for (const TCHAR* N : P.Plugins)
			{
				const bool bOn = IsPluginEnabled(N);
				On += bOn ? 1 : 0;
				G->SetBoolField(N, bOn);
			}
			G->SetStringField(TEXT("_summary"), FString::Printf(TEXT("%d/%d enabled"), On, P.Plugins.Num()));
			Groups->SetObjectField(P.Name, G);
		}
		Out->SetObjectField(TEXT("presets"), Groups);

		TSharedRef<FJsonObject> Other = MakeShared<FJsonObject>();
		for (const TCHAR* N : { TEXT("AIAssistant"), TEXT("Cascade"), TEXT("GaussianSplat"), TEXT("MassCharacters"), TEXT("NNERuntimeLiteRT"),
		                        TEXT("AssetProvenance"), TEXT("DebugDraw"), TEXT("ModularTestFramework"), TEXT("ChaosComposableSolver"), TEXT("LearningAgents") })
			Other->SetBoolField(N, IsPluginEnabled(N));
		Out->SetObjectField(TEXT("other_5_9_plugins"), Other);

		int32 ToolsetCount = 0, ToolCount = 0;
		if (FToolsetRegistry* Reg = FToolsetRegistry::Get())
		{
			Reg->ForEachToolset([&](const FString&, const FToolset& T)
			{
				if (!T.IsEnabled()) return;
				++ToolsetCount;
				ToolCount += T.ListToolNames().Num();
			});
		}
		Out->SetNumberField(TEXT("engine_toolsets_enabled"), ToolsetCount);
		Out->SetNumberField(TEXT("engine_tools_enabled"), ToolCount);
		Out->SetBoolField(TEXT("axivor_toolset_registered"), IsAxivorToolsetRegistered());
		Out->SetNumberField(TEXT("agent_skills_found"), MarkdownSkills::Discover().Num());
		Out->SetStringField(TEXT("hint"),
			TEXT("Enable a group with ue59_enable_preset(preset) (restart required). Call native toolsets with engine_call_tool. ")
			TEXT("Agent Skills: ue59_skills(action='list'). MCP coexistence: ue59_mcp_status."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue59_enable_preset(preset | plugins[])
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleEnablePreset(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FString> Names;
		const FString PresetName = ArgStr(Args, TEXT("preset"));
		if (!PresetName.IsEmpty())
		{
			const FPreset* P = FindPreset(PresetName);
			if (!P)
			{
				FString Known;
				for (const FPreset& K : Presets()) Known += FString(K.Name) + TEXT(" ");
				return Fail(FString::Printf(TEXT("Unknown preset '%s'. Known: %s"), *PresetName, *Known));
			}
			for (const TCHAR* N : P->Plugins) Names.AddUnique(N);
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args.IsValid() && Args->TryGetArrayField(TEXT("plugins"), Arr) && Arr)
			for (const TSharedPtr<FJsonValue>& V : *Arr) { FString S; if (V.IsValid() && V->TryGetString(S)) Names.AddUnique(S); }
		if (Names.IsEmpty())
			return Fail(TEXT("Provide preset (agents | blueprint_dsl | debug_perf | ui | canvas | animgen | metahuman_agents) and/or plugins[]."));

		const bool bEnable = ArgBool(Args, TEXT("enable"), true);
		TArray<TSharedPtr<FJsonValue>> Results;
		bool bAnyChanged = false;
		for (const FString& Name : Names)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("plugin"), Name);
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
			if (!Plugin.IsValid())
			{
				R->SetBoolField(TEXT("ok"), false);
				R->SetStringField(TEXT("error"), TEXT("plugin not found in this engine/project"));
			}
			else if (Plugin->IsEnabled() == bEnable)
			{
				R->SetBoolField(TEXT("ok"), true);
				R->SetStringField(TEXT("state"), bEnable ? TEXT("already enabled") : TEXT("already disabled"));
			}
			else
			{
				FText Reason;
				const bool bOk = IProjectManager::Get().SetPluginEnabled(Name, bEnable, Reason);
				R->SetBoolField(TEXT("ok"), bOk);
				if (!bOk) R->SetStringField(TEXT("error"), Reason.ToString()); else bAnyChanged = true;
			}
			Results.Add(MakeShared<FJsonValueObject>(R));
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (bAnyChanged)
		{
			FText SaveFail;
			const bool bSaved = IProjectManager::Get().SaveCurrentProjectToDisk(SaveFail);
			Out->SetBoolField(TEXT("project_saved"), bSaved);
			if (!bSaved) Out->SetStringField(TEXT("save_error"), SaveFail.ToString());
			Out->SetBoolField(TEXT("restart_required"), true);
		}
		Out->SetArrayField(TEXT("results"), Results);
		Out->SetStringField(TEXT("message"), bAnyChanged
			? TEXT("Plugin list updated in the .uproject. Restart the editor to load them. Most 5.9 agent plugins are Experimental.")
			: TEXT("No change."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue59_skills(action=list|read, key?, filter?)
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleSkills(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Action = ArgStr(Args, TEXT("action"), TEXT("list")).ToLower();
		if (Action == TEXT("list"))
		{
			const FString Filter = ArgStr(Args, TEXT("filter")).ToLower();
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const MarkdownSkills::FMarkdownSkill& S : MarkdownSkills::Discover())
			{
				if (!Filter.IsEmpty() && !S.Key.ToLower().Contains(Filter) && !S.Name.ToLower().Contains(Filter) && !S.Description.ToLower().Contains(Filter))
					continue;
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("key"), S.Key);
				O->SetStringField(TEXT("name"), S.Name);
				O->SetStringField(TEXT("description"), S.Description);
				Arr.Add(MakeShared<FJsonValueObject>(O));
			}
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("count"), Arr.Num());
			Out->SetArrayField(TEXT("skills"), Arr);
			Out->SetStringField(TEXT("hint"), TEXT("Read one with ue59_skills(action='read', key='/Skills/...'). Project skills live in <Project>/Skills/<name>/SKILL.md."));
			return Ok(Out);
		}
		if (Action == TEXT("read"))
		{
			const FString Key = ArgStr(Args, TEXT("key"));
			if (!MarkdownSkills::IsMarkdownSkillKey(Key))
				return Fail(TEXT("Provide key='/Skills/...' (see ue59_skills(action='list'))."));
			const TOptional<FString> Body = MarkdownSkills::LoadInstructions(Key);
			if (!Body.IsSet()) return Fail(FString::Printf(TEXT("No skill with key '%s'."), *Key));
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("key"), Key);
			Out->SetStringField(TEXT("instructions"), Body.GetValue());
			return Ok(Out);
		}
		return Fail(TEXT("Unknown action. Use: list | read."));
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue59_mcp_status — native Unreal MCP vs Axivor MCP coexistence
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleMcpStatus(const TSharedPtr<FJsonObject>&)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		const bool bPlugin = IsPluginEnabled(TEXT("ModelContextProtocol"));
		Out->SetBoolField(TEXT("native_mcp_plugin_enabled"), bPlugin);
		Out->SetBoolField(TEXT("native_mcp_module_loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("ModelContextProtocol")));
		if (TSharedPtr<FJsonObject> S = DumpSettings(TEXT("/Script/ModelContextProtocol.ModelContextProtocolSettings")))
			Out->SetObjectField(TEXT("native_mcp_editor_settings"), S);
		if (TSharedPtr<FJsonObject> S = DumpSettings(TEXT("/Script/ModelContextProtocol.ModelContextProtocolProjectSettings")))
			Out->SetObjectField(TEXT("native_mcp_project_settings"), S);
		Out->SetBoolField(TEXT("unreal_assistant_enabled"), IsPluginEnabled(TEXT("AIAssistant")));
		Out->SetBoolField(TEXT("axivor_toolset_registered"), IsAxivorToolsetRegistered());
		Out->SetStringField(TEXT("axivor_mcp_default_port"), TEXT("30000-30009 (path /mcp)"));
		Out->SetStringField(TEXT("guidance"),
			TEXT("The two servers do not collide: Unreal MCP defaults to http://127.0.0.1:8000/mcp, Axivor to :30000+/mcp. ")
			TEXT("Through Unreal MCP (and Unreal Assistant) every Axivor tool is reachable as the 'Axivor' toolset ")
			TEXT("(Axivor.SearchTools / GetToolDocs / CallTool) when ExposeToEngineToolsetRegistry=True (default). ")
			TEXT("With bEnableToolSearch the native server only lists list_toolsets/describe_toolsets/describe_tools/call_tool; ")
			TEXT("clients reach Axivor via call_tool(name='Axivor.CallTool'). Generate client configs with ")
			TEXT("engine_mcp_server(action='generate_client_config', client='ClaudeCode')."));
		return Ok(Out);
	}

	static const TCHAR* GTools[] = { TEXT("ue59_feature_status"), TEXT("ue59_enable_preset"), TEXT("ue59_skills"), TEXT("ue59_mcp_status") };
}

void FUECPUE59ExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("ue59_feature_status"), HandleFeatureStatus);
	D.RegisterHandler(TEXT("ue59_enable_preset"),  HandleEnablePreset);
	D.RegisterHandler(TEXT("ue59_skills"),         HandleSkills);
	D.RegisterHandler(TEXT("ue59_mcp_status"),     HandleMcpStatus);

	{
		const FName U(TEXT("ue59"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };
		Meta(TEXT("ue59_feature_status"), TEXT("UE 5.9 agentic stack status: which agent plugins/toolsets are enabled per preset (agents, blueprint_dsl, debug_perf, ui, canvas, animgen, metahuman_agents), engine toolset/tool counts, Axivor toolset registration, Agent Skills found."), TEXT(""));
		Meta(TEXT("ue59_enable_preset"), TEXT("Enable (or disable) a UE 5.9 plugin preset or explicit plugins in the .uproject (restart required)."), TEXT("preset?=agents|blueprint_dsl|debug_perf|ui|canvas|animgen|metahuman_agents, plugins?[], enable?=true"));
		Meta(TEXT("ue59_skills"), TEXT("UE 5.9 Markdown Agent Skills (engine, plugins, project): list them or read one skill's instructions."), TEXT("action=list|read, key?, filter?"));
		Meta(TEXT("ue59_mcp_status"), TEXT("Native Unreal MCP server settings (port, tool search, auto start), Unreal Assistant state and how it coexists with Axivor's MCP server and 'Axivor' toolset."), TEXT(""));
	}

	using UECPToolSafety::RegisterToolSafety;
	RegisterToolSafety(TEXT("ue59_feature_status"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("ue59_skills"),         EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("ue59_mcp_status"),     EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("ue59_enable_preset"),  EUECPToolSafety::Write);

	UE_LOG(LogUECPUE59Ext, Log, TEXT("Axivor UE 5.9 agentic pack registered (umbrella 'ue59')."));
}

void FUECPUE59ExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const TCHAR* N : GTools) D.UnregisterHandler(FName(N));
}

IMPLEMENT_MODULE(FUECPUE59ExtModule, UECPUE59Ext)
