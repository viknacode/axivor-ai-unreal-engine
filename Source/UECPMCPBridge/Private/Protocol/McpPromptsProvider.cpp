// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpPromptsProvider.h"

namespace McpPromptsProvider
{

namespace
{
	struct FPromptArg
	{
		const TCHAR* Name;
		const TCHAR* Description;
		bool         bRequired;
	};

	struct FPromptDef
	{
		const TCHAR*           Name;
		const TCHAR*           Description;
		TArrayView<const FPromptArg> Args;
		const TCHAR*           Body;
	};

	const FPromptArg ArgsBlueprintPath[] = {
		{ TEXT("blueprint_path"), TEXT("Asset path of the Blueprint to analyse, e.g. /Game/Player/BP_Player"), true },
	};
	const FPromptArg ArgsStateTreePath[] = {
		{ TEXT("state_tree_path"), TEXT("Asset path of the State Tree, e.g. /Game/AI/ST_EnemyCombat"), true },
	};
	const FPromptArg ArgsNiagaraPath[] = {
		{ TEXT("niagara_path"), TEXT("Asset path of the Niagara System, e.g. /Game/FX/NS_Explosion"), true },
	};
	const FPromptArg ArgsCompareBlueprints[] = {
		{ TEXT("blueprint_a"), TEXT("First Blueprint asset path"),  true },
		{ TEXT("blueprint_b"), TEXT("Second Blueprint asset path"), true },
	};
	const FPromptArg ArgsTopic[] = {
		{ TEXT("topic"), TEXT("What to research — e.g. 'best practices for Lyra ability cooldowns'"), true },
	};

	const FPromptDef Prompts[] = {
		{
			TEXT("analyse-blueprint"),
			TEXT("Deep-analyse a Blueprint: structure, variables, functions, event graph, and likely issues."),
			ArgsBlueprintPath,
			TEXT("Analyse the Blueprint at `{blueprint_path}` in depth.\n\n")
			TEXT("Start with `blueprint(action='get_blueprint_skeleton', blueprint_path='{blueprint_path}')` to load the structural overview (parent, interfaces, graphs, variables, components).")
			TEXT(" Then `blueprint(action='get_blueprint_graph', blueprint_path='{blueprint_path}', graph_name='<graph>')` for any graph that warrants a closer look.")
			TEXT(" Walk through:\n")
			TEXT("1. Inheritance chain and parent class\n")
			TEXT("2. Components and their configuration\n")
			TEXT("3. Variables — types, defaults, replication\n")
			TEXT("4. Event graph and function call flow\n")
			TEXT("5. Any anti-patterns, dead code, or obvious bugs\n\n")
			TEXT("Surface concrete findings with node IDs / variable names. Skip the praise.")
		},
		{
			TEXT("refactor-blueprint"),
			TEXT("Suggest concrete refactors for a Blueprint — readability, performance, splitting responsibilities."),
			ArgsBlueprintPath,
			TEXT("Propose a refactor pass for the Blueprint at `{blueprint_path}`.\n\n")
			TEXT("Start with `blueprint(action='get_blueprint_skeleton', blueprint_path='{blueprint_path}')` for the structural overview, then `blueprint(action='get_blueprint_graph', graph_name='<graph>')` on any graph that warrants a closer look.")
			TEXT(" Identify:\n")
			TEXT("- Functions doing too much (>20 nodes typically)\n")
			TEXT("- Repeated patterns that should become helper functions\n")
			TEXT("- Tick logic that could be event-driven\n")
			TEXT("- Variables that should be config or DataAssets\n")
			TEXT("- Hardcoded values that should be exposed\n\n")
			TEXT("For each, give a specific tool-call recipe (add_function / add_variable / etc.) the user can run. Don't refactor without asking — present the plan first.")
		},
		{
			TEXT("explain-state-tree"),
			TEXT("Walk through a State Tree's logic: states, transitions, tasks, and decision flow."),
			ArgsStateTreePath,
			TEXT("Explain the State Tree at `{state_tree_path}`.\n\n")
			TEXT("Fetch its structure with `state_tree(action='get_state_tree_summary', state_tree_path='{state_tree_path}')`")
			TEXT(" and walk through, in order:\n")
			TEXT("1. Root states and the decision flow between them\n")
			TEXT("2. Each state's tasks (what they do, in what order)\n")
			TEXT("3. Transitions (what triggers them, with what conditions)\n")
			TEXT("4. Blackboard / parameter dependencies\n\n")
			TEXT("Where the state machine has dead transitions or unreachable states, call them out.")
		},
		{
			TEXT("optimize-niagara"),
			TEXT("Audit a Niagara System for performance and suggest fixes."),
			ArgsNiagaraPath,
			TEXT("Audit `{niagara_path}` for performance.\n\n")
			TEXT("Use `niagara(action='get_niagara_detailed_summary', system_path='{niagara_path}')` to load all emitters, modules, and renderers.")
			TEXT(" Look for:\n")
			TEXT("- Emitters running on CPU when GPU would do (check sim target)\n")
			TEXT("- Particle counts > what's actually visible\n")
			TEXT("- Per-frame Spawn Rate that should be Spawn Burst\n")
			TEXT("- Scratch pad expressions doing trig per particle per frame\n")
			TEXT("- Renderers without distance LOD or culling\n\n")
			TEXT("Give a per-finding action (`set_niagara_module_parameter`, etc.) the user can apply.")
		},
		{
			TEXT("audit-project-performance"),
			TEXT("Run a 30-second profile capture in PIE and analyse the hot spots."),
			TArrayView<const FPromptArg>{},
			TEXT("Audit project performance.\n\n")
			TEXT("Run `play_test(action='start_profile_capture', duration_seconds=30)` to capture 30 seconds of PIE traces,")
			TEXT(" then `play_test(action='get_performance_report')` to read back the breakdown.")
			TEXT(" Focus on:\n")
			TEXT("- GameThread hot functions (top 10)\n")
			TEXT("- RenderThread bottlenecks\n")
			TEXT("- Memory allocation rate (GC pressure)\n")
			TEXT("- Anything taking >2ms per frame\n\n")
			TEXT("For each, suggest a concrete next step — profile a specific Blueprint, look at a specific tick, etc.")
		},
		{
			TEXT("compare-blueprints"),
			TEXT("Side-by-side comparison of two Blueprints — what's the same, what differs, what should be unified."),
			ArgsCompareBlueprints,
			TEXT("Compare `{blueprint_a}` and `{blueprint_b}`.\n\n")
			TEXT("Use `asset_management(action='compare_blueprints', blueprint_a='{blueprint_a}', blueprint_b='{blueprint_b}')`")
			TEXT(" if available; otherwise pull each summary individually and diff them.")
			TEXT(" Report:\n")
			TEXT("- Structural differences (parent class, components)\n")
			TEXT("- Variable additions / removals / type changes\n")
			TEXT("- Function additions / removals / signature changes\n")
			TEXT("- Whether the two should share a common base or interface — and which functions/variables would move there\n")
		},
		{
			TEXT("research-topic"),
			TEXT("Research a UE topic via web search and summarise actionable findings."),
			ArgsTopic,
			TEXT("Research `{topic}` for this UE 5 project.\n\n")
			TEXT("Use `search(query='{topic}')` and follow up with focused queries on whatever the first pass surfaces.")
			TEXT(" Synthesise findings into:\n")
			TEXT("1. The 3-5 most authoritative recent sources (2024+ where possible)\n")
			TEXT("2. Concrete patterns / APIs the user should know about\n")
			TEXT("3. Common pitfalls\n")
			TEXT("4. A suggested next step in this project\n")
		},
	};

	const FPromptDef* FindPrompt(const FString& Name)
	{
		for (const FPromptDef& P : Prompts)
		{
			if (Name == P.Name) return &P;
		}
		return nullptr;
	}

	FString SubstituteArgs(const FString& Template, const TSharedPtr<FJsonObject>& Arguments)
	{
		if (!Arguments.IsValid()) return Template;
		FString Out = Template;
		for (const auto& Pair : Arguments->Values)
		{
			FString Value;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
			{
				Out = Out.Replace(*FString::Printf(TEXT("{%s}"), *Pair.Key), *Value, ESearchCase::CaseSensitive);
			}
		}
		return Out;
	}
}

TSharedRef<FJsonObject> BuildPromptsListResult()
{
	TArray<TSharedPtr<FJsonValue>> PromptArray;
	PromptArray.Reserve(UE_ARRAY_COUNT(Prompts));

	for (const FPromptDef& P : Prompts)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),        P.Name);
		Entry->SetStringField(TEXT("description"), P.Description);

		TArray<TSharedPtr<FJsonValue>> ArgArray;
		ArgArray.Reserve(P.Args.Num());
		for (const FPromptArg& A : P.Args)
		{
			TSharedRef<FJsonObject> Arg = MakeShared<FJsonObject>();
			Arg->SetStringField(TEXT("name"),        A.Name);
			Arg->SetStringField(TEXT("description"), A.Description);
			Arg->SetBoolField  (TEXT("required"),    A.bRequired);
			ArgArray.Add(MakeShared<FJsonValueObject>(Arg));
		}
		Entry->SetArrayField(TEXT("arguments"), ArgArray);

		PromptArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("prompts"), PromptArray);
	return Result;
}

TSharedRef<FJsonObject> HandlePromptsGet(const TSharedPtr<FJsonObject>& Params)
{
	const auto MakeError = [](const FString& Msg) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("description"), Msg);
		TArray<TSharedPtr<FJsonValue>> Messages;
		R->SetArrayField(TEXT("messages"), Messages);
		return R;
	};

	if (!Params.IsValid())
	{
		return MakeError(TEXT("prompts/get: missing params"));
	}

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return MakeError(TEXT("prompts/get: missing params.name"));
	}

	const FPromptDef* Def = FindPrompt(Name);
	if (!Def)
	{
		return MakeError(FString::Printf(TEXT("Unknown prompt: %s"), *Name));
	}

	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	TSharedPtr<FJsonObject> Arguments;
	if (Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
	{
		Arguments = *ArgsObj;
	}

	for (const FPromptArg& Arg : Def->Args)
	{
		if (!Arg.bRequired) continue;
		FString Tmp;
		const bool bHas = Arguments.IsValid() && Arguments->TryGetStringField(Arg.Name, Tmp) && !Tmp.IsEmpty();
		if (!bHas)
		{
			return MakeError(FString::Printf(
				TEXT("Prompt '%s' requires argument '%s' (%s)."),
				*Name, Arg.Name, Arg.Description));
		}
	}

	const FString Rendered = SubstituteArgs(Def->Body, Arguments);

	TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
	Content->SetStringField(TEXT("type"), TEXT("text"));
	Content->SetStringField(TEXT("text"), Rendered);

	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("role"), TEXT("user"));
	Message->SetObjectField(TEXT("content"), Content);

	TArray<TSharedPtr<FJsonValue>> Messages;
	Messages.Add(MakeShared<FJsonValueObject>(Message));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("description"), Def->Description);
	Result->SetArrayField (TEXT("messages"),    Messages);
	return Result;
}

}
