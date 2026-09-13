// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPOpenAIAgentLoop.h"
#include "Misc/EngineVersionComparison.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCrewService.h"
#include "Types/CallerContext.h"
#include "Containers/Ticker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ToJsonStr(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Str)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
		FJsonSerializer::Deserialize(R, Obj);
		return Obj;
	}

	TSharedPtr<FJsonValue> MakeToolSchema(
		const FString& Name,
		const FString& Description,
		bool bRequireAction)
	{
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("type"), TEXT("object"));
		Params->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		Params->SetBoolField(TEXT("additionalProperties"), true);
		if (bRequireAction)
		{
			TArray<TSharedPtr<FJsonValue>> Required;
			Required.Add(MakeShared<FJsonValueString>(TEXT("action")));
			Params->SetArrayField(TEXT("required"), Required);
		}

		TSharedRef<FJsonObject> Func = MakeShared<FJsonObject>();
		Func->SetStringField(TEXT("name"), Name);
		Func->SetStringField(TEXT("description"), Description);
		Func->SetObjectField(TEXT("parameters"), Params);

		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("type"), TEXT("function"));
		Tool->SetObjectField(TEXT("function"), Func);
		return MakeShared<FJsonValueObject>(Tool);
	}
}

TArray<TSharedPtr<FJsonValue>> FUECPOpenAIAgentLoop::BuildToolDefinitions()
{
	TArray<TSharedPtr<FJsonValue>> Tools;
	auto AddOpenAITool = [&](const FString& Name, const FString& Desc,
		const TSharedRef<FJsonObject>& Props, bool bRequireAction)
	{
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("type"), TEXT("object"));
		Params->SetObjectField(TEXT("properties"), Props);
		Params->SetBoolField(TEXT("additionalProperties"), true);
		if (bRequireAction)
			Params->SetArrayField(TEXT("required"),
				TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("action")) });
		TSharedRef<FJsonObject> Func = MakeShared<FJsonObject>();
		Func->SetStringField(TEXT("name"), Name);
		Func->SetStringField(TEXT("description"), Desc);
		Func->SetObjectField(TEXT("parameters"), Params);
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("type"), TEXT("function"));
		Tool->SetObjectField(TEXT("function"), Func);
		Tools.Add(MakeShared<FJsonValueObject>(Tool));
	};

	for (const FUECPToolCatalogEntry& E : GetStandardToolEntries())
	{
		if (FString(E.Name) == TEXT("get_tool_docs"))
		{
			TSharedRef<FJsonObject> CatProp = MakeShared<FJsonObject>();
			CatProp->SetStringField(TEXT("type"), TEXT("string"));
			CatProp->SetStringField(TEXT("description"), TEXT("Single category name (e.g. 'blueprint'). Omit when using categories[]."));
			TSharedRef<FJsonObject> CatsItems = MakeShared<FJsonObject>();
			CatsItems->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> CatsProp = MakeShared<FJsonObject>();
			CatsProp->SetStringField(TEXT("type"), TEXT("array"));
			CatsProp->SetObjectField(TEXT("items"), CatsItems);
			CatsProp->SetStringField(TEXT("description"), TEXT("Batch: list of category names."));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("category"), CatProp);
			Props->SetObjectField(TEXT("categories"), CatsProp);
			AddOpenAITool(TEXT("get_tool_docs"), FString(E.Description), Props, false);
		}
		else if (FString(E.Name) == TEXT("project_plan"))
		{
			TSharedRef<FJsonObject> ActionProp = MakeShared<FJsonObject>();
			ActionProp->SetStringField(TEXT("type"), TEXT("string"));
			ActionProp->SetStringField(TEXT("description"), TEXT("create_plan | get_plan | clear_plan | list_plans | import_plan"));
			TSharedRef<FJsonObject> TitleProp = MakeShared<FJsonObject>();
			TitleProp->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> CtxProp = MakeShared<FJsonObject>();
			CtxProp->SetStringField(TEXT("type"), TEXT("string"));
			CtxProp->SetStringField(TEXT("description"), TEXT("The brief body: goal, assets + their paths, conventions/guidelines, key decisions."));
			TSharedRef<FJsonObject> SrcProp = MakeShared<FJsonObject>();
			SrcProp->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("action"), ActionProp);
			Props->SetObjectField(TEXT("title"), TitleProp);
			Props->SetObjectField(TEXT("context"), CtxProp);
			Props->SetObjectField(TEXT("source_conv_id"), SrcProp);
			AddOpenAITool(TEXT("project_plan"), FString(E.Description), Props, true);
		}
		else if (FString(E.Name) == TEXT("memory"))
		{
			TSharedRef<FJsonObject> MemActionProp = MakeShared<FJsonObject>();
			MemActionProp->SetStringField(TEXT("type"), TEXT("string"));
			MemActionProp->SetStringField(TEXT("description"), TEXT("add_memory | suggest_memory | get_memories | delete_memory"));
			TSharedRef<FJsonObject> ContentProp = MakeShared<FJsonObject>();
			ContentProp->SetStringField(TEXT("type"), TEXT("string"));
			ContentProp->SetStringField(TEXT("description"), TEXT("Memory text (add_memory / suggest_memory)."));
			TSharedRef<FJsonObject> CatProp = MakeShared<FJsonObject>();
			CatProp->SetStringField(TEXT("type"), TEXT("string"));
			CatProp->SetStringField(TEXT("description"), TEXT("project_info | recent_work | preferences | patterns | asset_relations | decisions"));
			TSharedRef<FJsonObject> MemIdProp = MakeShared<FJsonObject>();
			MemIdProp->SetStringField(TEXT("type"), TEXT("integer"));
			MemIdProp->SetStringField(TEXT("description"), TEXT("Memory id to delete (delete_memory)."));
			TSharedRef<FJsonObject> MemProps = MakeShared<FJsonObject>();
			MemProps->SetObjectField(TEXT("action"), MemActionProp);
			MemProps->SetObjectField(TEXT("content"), ContentProp);
			MemProps->SetObjectField(TEXT("category"), CatProp);
			MemProps->SetObjectField(TEXT("memory_id"), MemIdProp);
			AddOpenAITool(TEXT("memory"), FString(E.Description), MemProps, true);
		}
		else if (E.InputSchema.IsValid())
		{
			// Extension MCP proxy tools carry their server-provided schema; umbrellas carry the
			// metadata-derived one. Strict mode is intentionally not requested: the schemas keep
			// additionalProperties=true so undocumented params still reach the handlers.
			TSharedRef<FJsonObject> Func = MakeShared<FJsonObject>();
			Func->SetStringField(TEXT("name"), E.Name);
			Func->SetStringField(TEXT("description"), E.Description);
			Func->SetObjectField(TEXT("parameters"), E.InputSchema.ToSharedRef());
			TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("type"), TEXT("function"));
			Tool->SetObjectField(TEXT("function"), Func);
			Tools.Add(MakeShared<FJsonValueObject>(Tool));
		}
		else
		{
			Tools.Add(MakeToolSchema(E.Name, E.Description, E.bIsUmbrella));
		}
	}
	return Tools;
}

TArray<TSharedPtr<FJsonValue>> FUECPOpenAIAgentLoop::ConvertHistoryEntryToOpenAI(
	const TSharedPtr<FJsonValue>& GeminiEntry)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!GeminiEntry.IsValid() || GeminiEntry->Type != EJson::Object) return Out;

	TSharedPtr<FJsonObject> Obj = GeminiEntry->AsObject();
	FString Role;
	if (!Obj->TryGetStringField(TEXT("role"), Role)) return Out;

	if (Role == TEXT("context") || Role == TEXT("tool_bubble") ||
		Role == TEXT("agent_thinking") || Role == TEXT("spinner")) return Out;

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!Obj->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->IsEmpty()) return Out;

	const FString OARole = (Role == TEXT("model")) ? TEXT("assistant") : TEXT("user");

	bool bHasImage = false;
	for (const TSharedPtr<FJsonValue>& PV : *Parts)
	{
		TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
		if (P && P->HasField(TEXT("inline_data"))) { bHasImage = true; break; }
	}

	TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), OARole);

	if (!bHasImage)
	{
		FString Text;
		for (const TSharedPtr<FJsonValue>& PV : *Parts)
		{
			TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
			FString T;
			if (P && P->TryGetStringField(TEXT("text"), T)) Text += T;
		}
		if (Text.IsEmpty()) return Out;
		Msg->SetStringField(TEXT("content"), Text);
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> Content;
		for (const TSharedPtr<FJsonValue>& PV : *Parts)
		{
			TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
			if (!P) continue;

			FString T;
			if (P->TryGetStringField(TEXT("text"), T))
			{
				TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
				Block->SetStringField(TEXT("type"), TEXT("text"));
				Block->SetStringField(TEXT("text"), T);
				Content.Add(MakeShared<FJsonValueObject>(Block));
			}
			const TSharedPtr<FJsonObject>* InlineData = nullptr;
			if (P->TryGetObjectField(TEXT("inline_data"), InlineData))
			{
				FString Mime, Data;
				(*InlineData)->TryGetStringField(TEXT("mime_type"), Mime);
				(*InlineData)->TryGetStringField(TEXT("data"), Data);
				TSharedRef<FJsonObject> ImgUrl = MakeShared<FJsonObject>();
				ImgUrl->SetStringField(TEXT("url"),
					FString::Printf(TEXT("data:%s;base64,%s"), *Mime, *Data));
				TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
				Block->SetStringField(TEXT("type"), TEXT("image_url"));
				Block->SetObjectField(TEXT("image_url"), ImgUrl);
				Content.Add(MakeShared<FJsonValueObject>(Block));
			}
		}
		if (Content.IsEmpty()) return Out;
		Msg->SetArrayField(TEXT("content"), Content);
	}

	Out.Add(MakeShared<FJsonValueObject>(Msg));
	return Out;
}

void FUECPOpenAIAgentLoop::Start()
{
	check(IsInGameThread());
	State = EState::Streaming;
	SendRound();
}

FString FUECPOpenAIAgentLoop::ChooseTaskTier() const
{
	if (InteractionMode == EAIInteractionMode::JustChat)
	{
		return TEXT("light");
	}

	static const TSet<FString> ComplexToolNames = {
		TEXT("build_blueprint_graph"), TEXT("clear_blueprint_graph"),
		TEXT("place_node"), TEXT("connect_pins"), TEXT("set_pin_default"),
		TEXT("delete_nodes"), TEXT("compile_blueprint"),
		TEXT("add_function"), TEXT("set_function_pure"),
		TEXT("create_asset"),
		TEXT("create_widget_from_layout"), TEXT("set_widget_property"),
		TEXT("set_widget_canvas_size"), TEXT("set_widget_slot"),
		TEXT("build_material_graph"),
	};
	static const TSet<FString> ComplexUmbrellaActions = {
		TEXT("blueprint"), TEXT("widget"), TEXT("niagara"),
		TEXT("material"), TEXT("animation"), TEXT("behavior_tree"),
		TEXT("state_tree"), TEXT("control_rig"),
	};

	auto MatchesComplex = [](const FString& Name, const FString& ArgsJson) -> bool
	{
		if (ComplexToolNames.Contains(Name)) return true;
		if (!ComplexUmbrellaActions.Contains(Name)) return false;
		const int32 ActionIdx = ArgsJson.Find(TEXT("\"action\""));
		if (ActionIdx == INDEX_NONE) return true;
		int32 ColonIdx = ArgsJson.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ActionIdx + 8);
		if (ColonIdx == INDEX_NONE) return true;
		int32 QuoteStart = ArgsJson.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ColonIdx + 1);
		if (QuoteStart == INDEX_NONE) return true;
		int32 QuoteEnd = ArgsJson.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, QuoteStart + 1);
		if (QuoteEnd == INDEX_NONE) return true;
		FString Action = ArgsJson.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		return ComplexToolNames.Contains(Action);
	};

	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<FJsonObject> Msg = History[i].IsValid() ? History[i]->AsObject() : nullptr;
		if (!Msg.IsValid()) continue;
		FString Role;
		Msg->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("assistant")) continue;
		const TArray<TSharedPtr<FJsonValue>>* TCs = nullptr;
		if (!Msg->TryGetArrayField(TEXT("tool_calls"), TCs) || !TCs) continue;
		for (const TSharedPtr<FJsonValue>& V : *TCs)
		{
			TSharedPtr<FJsonObject> TC = V.IsValid() ? V->AsObject() : nullptr;
			if (!TC.IsValid()) continue;
			const TSharedPtr<FJsonObject>* FuncObj = nullptr;
			if (!TC->TryGetObjectField(TEXT("function"), FuncObj) || !FuncObj) continue;
			FString FName_, FArgs;
			(*FuncObj)->TryGetStringField(TEXT("name"), FName_);
			(*FuncObj)->TryGetStringField(TEXT("arguments"), FArgs);
			if (MatchesComplex(FName_, FArgs)) return TEXT("heavy");
		}
	}

	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<FJsonObject> Msg = History[i].IsValid() ? History[i]->AsObject() : nullptr;
		if (!Msg.IsValid()) continue;
		FString Role;
		Msg->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("user")) continue;

		FString Content;
		Msg->TryGetStringField(TEXT("content"), Content);
		const FString Trimmed = Content.TrimStartAndEnd();
		const FString Lower = Trimmed.ToLower();

		static const TArray<FString> ComplexKeywords = {
			TEXT("blueprint"), TEXT("widget"), TEXT("graph"), TEXT("function"),
			TEXT("event"), TEXT("niagara"), TEXT("material"), TEXT("animation"),
			TEXT("behavior tree"), TEXT("state tree"), TEXT("ability"), TEXT("inventory"),
			TEXT("hud"), TEXT("ui "), TEXT("umg"), TEXT("system"), TEXT("feature"),
			TEXT("create"), TEXT("build"), TEXT("implement"), TEXT("add"),
			TEXT("fix"), TEXT("modify"), TEXT("refactor"), TEXT("setup"), TEXT("set up"),
		};
		for (const FString& K : ComplexKeywords) if (Lower.Contains(K)) return TEXT("heavy");

		if (Trimmed.Len() < 30) return TEXT("light");

		return TEXT("heavy");
	}

	return TEXT("heavy");
}

void FUECPOpenAIAgentLoop::SendRound()
{
	check(IsInGameThread());

	if (bStopped || Round >= MaxRounds)
	{
		Finish(false, bStopped ? TEXT("") : FString::Printf(
			TEXT("Reached the %d-round limit. Type a follow-up message to continue."), MaxRounds));
		return;
	}
	++Round;

	PendingToolCalls.Reset();
	TextAccum.Empty();
	ReasoningAccum.Empty();
	bInThinkBlock = false;
	PendingContentTail.Empty();
	FinishReason.Empty();
	FinalInputTokens  = 0;
	FinalOutputTokens = 0;
	ResetSseState();

	WindowHistoryForRequest();

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> SysMsg = MakeShared<FJsonObject>();
	SysMsg->SetStringField(TEXT("role"), TEXT("system"));
	SysMsg->SetStringField(TEXT("content"), SystemPrompt);
	Messages.Add(MakeShared<FJsonValueObject>(SysMsg));
	Messages.Append(History);

	for (TSharedPtr<FJsonValue>& MV : Messages)
	{
		TSharedPtr<FJsonObject> M = MV.IsValid() ? MV->AsObject() : nullptr;
		if (!M.IsValid()) continue;
		FString R;
		M->TryGetStringField(TEXT("role"), R);
		if (R != TEXT("assistant")) continue;
		FString RC;
		if (!M->TryGetStringField(TEXT("reasoning_content"), RC))
		{
			M->SetStringField(TEXT("reasoning_content"), TEXT(""));
		}
	}

	TSharedRef<FJsonObject> StreamOptions = MakeShared<FJsonObject>();
	StreamOptions->SetBoolField(TEXT("include_usage"), true);

	const FString ModelLower = ModelName.ToLower();
	const bool bUsesCompletionTokens =
		ModelLower.StartsWith(TEXT("gpt-5"))  ||
		ModelLower.StartsWith(TEXT("o1"))     ||
		ModelLower.StartsWith(TEXT("o3"))     ||
		ModelLower.StartsWith(TEXT("o4"))     ||
		ModelLower.StartsWith(TEXT("o-"));
	const FString TokensKey = bUsesCompletionTokens
		? TEXT("max_completion_tokens") : TEXT("max_tokens");

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("model"), ModelName);
	Payload->SetNumberField(TokensKey, ResolveMaxOutputTokens());
	Payload->SetBoolField(TEXT("stream"), true);
	Payload->SetObjectField(TEXT("stream_options"), StreamOptions);
	Payload->SetArrayField(TEXT("tools"), BuildToolDefinitions());
	Payload->SetArrayField(TEXT("messages"), Messages);

	FString Body;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Payload, Writer);
	Writer->Close();

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetURL(EndpointURL);
	ActiveRequest->SetHeader(TEXT("Content-Type"),  TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	for (const auto& KV : ExtraHeaders)
		ActiveRequest->SetHeader(KV.Key, KV.Value);

	if (ExtraHeaders.Contains(TEXT("x-license-token")))
	{
		const FString Tier = ChooseTaskTier();
		ActiveRequest->SetHeader(TEXT("x-uecp-task-tier"), Tier);
		UE_LOG(LogTemp, Verbose, TEXT("[FreeTier] x-uecp-task-tier=%s (round %d)"), *Tier, Round);
	}
	ActiveRequest->SetContentAsString(Body);
	ConfigureRequestTimeout();

	TWeakPtr<FUECPOpenAIAgentLoop> WeakSelf = AsShared();

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	ActiveRequest->SetResponseBodyReceiveStreamDelegateV2(
		FHttpRequestStreamDelegateV2::CreateLambda(
			[WeakSelf](void* Ptr, int64& Length)
			{
				TSharedPtr<FUECPOpenAIAgentLoop> Self = WeakSelf.Pin();
				if (!Self || !Ptr || Length <= 0) return;
				Self->OnStreamChunk(Ptr, Length);
			}));
#endif

	ActiveRequest->OnRequestProgress64().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, uint64 Sent, uint64 Recv)
		{
			TSharedPtr<FUECPOpenAIAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestProgress(Req, Sent, Recv);
		});

	ActiveRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			TSharedPtr<FUECPOpenAIAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestComplete(Req, Resp, bOk);
		});

	ActiveRequest->ProcessRequest();
}

void FUECPOpenAIAgentLoop::DrainSseBuffer(const FString& NewData)
{
	FString Combined = SseRemnant + NewData;
	SseRemnant.Empty();

	int32 Start = 0;
	while (Start < Combined.Len())
	{
		int32 NL = Combined.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (NL == INDEX_NONE)
		{
			SseRemnant = Combined.Mid(Start);
			break;
		}

		FString Line = Combined.Mid(Start, NL - Start).TrimStartAndEnd();
		Start = NL + 1;

		if (!Line.StartsWith(TEXT("data: "))) continue;

		FString Data = Line.Mid(6);
		if (Data == TEXT("[DONE]")) continue;

		ProcessSseData(Data);
	}
}

void FUECPOpenAIAgentLoop::ProcessSseData(const FString& DataJson)
{
	if (bStopped) return;

	TSharedPtr<FJsonObject> Root = ParseJson(DataJson);
	if (!Root.IsValid()) return;

	const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr)
	{
		int32 Prompt = 0, Completion = 0;
		(*UsagePtr)->TryGetNumberField(TEXT("prompt_tokens"), Prompt);
		(*UsagePtr)->TryGetNumberField(TEXT("completion_tokens"), Completion);
		if (Prompt > 0)     FinalInputTokens  = Prompt;
		if (Completion > 0) FinalOutputTokens = Completion;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->IsEmpty()) return;

	TSharedPtr<FJsonObject> Choice = (*Choices)[0].IsValid() ? (*Choices)[0]->AsObject() : nullptr;
	if (!Choice.IsValid()) return;

	FString FR;
	if (Choice->TryGetStringField(TEXT("finish_reason"), FR) && !FR.IsEmpty() && FR != TEXT("null"))
		FinishReason = FR;

	const TSharedPtr<FJsonObject>* DeltaPtr = nullptr;
	if (!Choice->TryGetObjectField(TEXT("delta"), DeltaPtr) || !DeltaPtr) return;
	const TSharedPtr<FJsonObject>& Delta = *DeltaPtr;

	FString ReasoningDelta;
	if (Delta->TryGetStringField(TEXT("reasoning_content"), ReasoningDelta) && !ReasoningDelta.IsEmpty())
	{
		ReasoningAccum += ReasoningDelta;
	}
	else if (Delta->TryGetStringField(TEXT("reasoning"), ReasoningDelta) && !ReasoningDelta.IsEmpty())
	{
		ReasoningAccum += ReasoningDelta;
	}

	FString Content;
	if (Delta->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
	{
		PendingContentTail += Content;
		while (true)
		{
			if (bInThinkBlock)
			{
				const int32 EndPos = PendingContentTail.Find(TEXT("</think>"), ESearchCase::CaseSensitive);
				if (EndPos == INDEX_NONE)
				{
					const int32 SafeLen = FMath::Max(0, PendingContentTail.Len() - 7);
					ReasoningAccum += PendingContentTail.Left(SafeLen);
					PendingContentTail.RightChopInline(SafeLen, EAllowShrinking::No);
					break;
				}
				ReasoningAccum += PendingContentTail.Left(EndPos);
				PendingContentTail.RightChopInline(EndPos + 8, EAllowShrinking::No);
				bInThinkBlock = false;
			}
			else
			{
				const int32 StartPos = PendingContentTail.Find(TEXT("<think>"), ESearchCase::CaseSensitive);
				if (StartPos == INDEX_NONE)
				{
					const int32 BufLen = PendingContentTail.Len();
					int32 HoldLen = 0;
					static const TCHAR* const ThinkTag = TEXT("<think");
					const int32 TagLen = 6;
					for (int32 N = FMath::Min(TagLen, BufLen); N > 0; --N)
					{
						bool bMatch = true;
						for (int32 I = 0; I < N; ++I)
						{
							if (PendingContentTail[BufLen - N + I] != ThinkTag[I]) { bMatch = false; break; }
						}
						if (bMatch) { HoldLen = N; break; }
					}
					const int32 SafeLen = BufLen - HoldLen;
					if (SafeLen > 0)
					{
						const FString Plain = PendingContentTail.Left(SafeLen);
						TextAccum += Plain;
						if (OnTextDelta) OnTextDelta(Plain);
					}
					PendingContentTail.RightChopInline(SafeLen, EAllowShrinking::No);
					break;
				}
				if (StartPos > 0)
				{
					const FString Plain = PendingContentTail.Left(StartPos);
					TextAccum += Plain;
					if (OnTextDelta) OnTextDelta(Plain);
				}
				PendingContentTail.RightChopInline(StartPos + 7, EAllowShrinking::No);
				bInThinkBlock = true;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolCallDeltas = nullptr;
	if (!Delta->TryGetArrayField(TEXT("tool_calls"), ToolCallDeltas) || !ToolCallDeltas) return;

	for (const TSharedPtr<FJsonValue>& TV : *ToolCallDeltas)
	{
		TSharedPtr<FJsonObject> TC = TV.IsValid() ? TV->AsObject() : nullptr;
		if (!TC.IsValid()) continue;

		int32 Index = 0;
		TC->TryGetNumberField(TEXT("index"), Index);

		FToolCallAccum& Accum = PendingToolCalls.FindOrAdd(Index);

		FString Id;
		if (TC->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty())
			Accum.Id = Id;

		const TSharedPtr<FJsonObject>* FuncPtr = nullptr;
		if (!TC->TryGetObjectField(TEXT("function"), FuncPtr) || !FuncPtr) continue;

		FString Name;
		if ((*FuncPtr)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
			Accum.Name = Name;

		FString Args;
		if ((*FuncPtr)->TryGetStringField(TEXT("arguments"), Args))
			Accum.ArgumentsAccum += Args;
	}
}

void FUECPOpenAIAgentLoop::OnRequestComplete(FHttpRequestPtr, FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	check(IsInGameThread());
	if (bStopped) return;

	FString StreamedBody;
	{
		FString Remaining;
		{
			FScopeLock Lock(&SseMutex);
			Remaining     = MoveTemp(SseBuffer); SseBuffer.Empty();
			StreamedBody  = RawResponseBody;
		}
		if (Remaining.IsEmpty() && StreamedBody.IsEmpty() && Response.IsValid())
		{
			Remaining = RecoverResponseBody(Response, StreamedBody);
			Remaining.ReplaceInline(TEXT("\r"), TEXT(""));
			StreamedBody = Remaining;
		}
		if (!Remaining.IsEmpty()) DrainSseBuffer(Remaining);
	}

	if (!PendingContentTail.IsEmpty())
	{
		if (bInThinkBlock)
		{
			ReasoningAccum += PendingContentTail;
		}
		else
		{
			TextAccum += PendingContentTail;
			if (OnTextDelta) OnTextDelta(PendingContentTail);
		}
		PendingContentTail.Empty();
	}

	if (OnRoundResponse && Response.IsValid()) OnRoundResponse(Response);

	const FString PName = ProviderName.IsEmpty() ? TEXT("the provider") : ProviderName;

	const bool bRoundHadOutput =
		!TextAccum.IsEmpty() || !ReasoningAccum.IsEmpty() || PendingToolCalls.Num() > 0;

	if (!bWasSuccessful || !Response.IsValid())
	{
		if (TryScheduleTransientRetry(bWasSuccessful, 0, FString(), Response, PName, bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, 0, TEXT(""), PName));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		const FString Body = RecoverResponseBody(Response, StreamedBody);
		if (TryScheduleTransientRetry(bWasSuccessful, Code, Body, Response, PName, bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, Code, Body, PName));
		return;
	}

	TransientRetryCount = 0;

	if (PendingToolCalls.Num() > 0)
	{
		ToolCallsThisLoop += PendingToolCalls.Num();
		State = EState::Dispatching;
		DispatchToolsAndLoop();
	}
	else if (FinishReason == TEXT("length") && TextAccum.IsEmpty())
	{
		Finish(false, TEXT("Response cut off (output token limit reached). Send a follow-up to continue."));
	}
	else
	{
		const bool bLengthCutoff = (FinishReason == TEXT("length"));
		const bool bIntentStall  = false;
		if (bLengthCutoff || bIntentStall)
		{
			const FString Footer = bLengthCutoff
				? TEXT("\n\n---\n*Response cut off (token limit). Type \"continue\" to resume.*")
				: TEXT("\n\n---\n*Agent ended its turn without firing tools. Type \"continue\" if it should keep going.*");
			TextAccum += Footer;
			if (OnTextDelta) OnTextDelta(Footer);
			bStalledNeedsContinue = true;
		}
		AppendAssistantMessage();
		Finish(true);
	}
}

void FUECPOpenAIAgentLoop::DispatchToolsAndLoop()
{
	check(IsInGameThread());

	TArray<int32> Indices;
	PendingToolCalls.GetKeys(Indices);
	Indices.Sort();

	for (int32 Idx : Indices)
	{
		const FToolCallAccum& TC = PendingToolCalls[Idx];
		TSharedPtr<FJsonObject> LA = ParseJson(TC.ArgumentsAccum);
		FString LAction;
		if (LA.IsValid()) LA->TryGetStringField(TEXT("action"), LAction);
		const FString LLabel = LAction.IsEmpty() ? TC.Name
			: FString::Printf(TEXT("%s \xB7 %s"), *TC.Name, *LAction);
		if (OnToolStart) OnToolStart(TC.Id, LLabel, TC.ArgumentsAccum.Left(200));
	}

	DispatchOneTool(MoveTemp(Indices), 0, TArray<TTuple<FString, FString, bool>>{});
}

void FUECPOpenAIAgentLoop::DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
	TArray<TTuple<FString, FString, bool>> Results)
{
	if (bStopped.load()) return;

	if (NextIndex >= Indices.Num())
	{
		FinaliseDispatchAndContinue(Results);
		return;
	}

	IUECPToolDispatcher* Dispatcher = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetToolDispatcher() : nullptr;

	const FToolCallAccum& TC = PendingToolCalls[Indices[NextIndex]];
	const FString  TCId = TC.Id;
	const FString  TCName = TC.Name;
	const FString  TCArgs = TC.ArgumentsAccum;

	if (!Dispatcher)
	{
		const FString ErrJson = TEXT("{\"error\":\"Tool dispatcher unavailable.\"}");
		if (OnToolResult) OnToolResult(TCId, TCName, false, ErrJson);
		Results.Emplace(TCId, ErrJson, false);
		DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
		return;
	}

	TSharedPtr<FJsonObject> ArgsObj = ParseJson(TCArgs);
	if (!ArgsObj.IsValid()) ArgsObj = MakeShared<FJsonObject>();

	FString LAction2;
	if (ArgsObj.IsValid()) ArgsObj->TryGetStringField(TEXT("action"), LAction2);
	const FString Label = LAction2.IsEmpty() ? TCName
		: FString::Printf(TEXT("%s \xB7 %s"), *TCName, *LAction2);

	FName DispatchName = ResolveDispatchName(TCName, ArgsObj);

	{
		FString CrewDeny;
		if (!IUECPCoreModule::Get().GetCrewService().IsToolAllowedForChat(ChatID, DispatchName, CrewDeny))
		{
			const FString Msg = FString::Printf(TEXT("crew role denies tool: %s"), *CrewDeny);
			if (OnToolResult) OnToolResult(TCId, Label, false, Msg);
			Results.Emplace(TCId, Msg, false);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}
	}

	UECPCallerContext::WriteCallerChatId(ArgsObj, ChatID);
	{
		FUECPToolResult TR;
		if (OnWidgetTool && OnWidgetTool(TCName, ArgsObj, TR))
		{
			FString ResultJson = !TR.ResultJson.IsEmpty() ? TR.ResultJson : TR.ErrorMessage;
			if (ResultJson.IsEmpty()) ResultJson = TEXT("Tool dispatch failed.");
			if (OnToolResult) OnToolResult(TCId, Label, TR.bSuccess, ResultJson);
			Results.Emplace(TCId, ResultJson, TR.bSuccess);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}
	}

	FString DenyReason;
	if (OnBeforeDispatch && !OnBeforeDispatch(TCName, ArgsObj, DispatchName, DenyReason))
	{
		if (!DenyReason.IsEmpty())
		{
			if (OnToolResult) OnToolResult(TCId, Label, false, DenyReason);
			Results.Emplace(TCId, DenyReason, false);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}

		PausedDispatchRemainingIndices.Reset();
		for (int32 j = NextIndex; j < Indices.Num(); ++j)
			PausedDispatchRemainingIndices.Add(Indices[j]);
		PausedDispatchPartialResults = MoveTemp(Results);
		bPendingSendRound = true;
		return;
	}

	Dispatcher->ExecuteFromArgsAsync(DispatchName, ArgsObj,
		[this, Indices = MoveTemp(Indices), NextIndex, Results = MoveTemp(Results),
		 TCId, Label](FUECPToolResult TR) mutable
		{
			if (bStopped.load()) return;
			FString ResultJson = !TR.ResultJson.IsEmpty() ? TR.ResultJson : TR.ErrorMessage;
			if (ResultJson.IsEmpty()) ResultJson = TEXT("Tool dispatch failed.");
			if (OnToolResult) OnToolResult(TCId, Label, TR.bSuccess, ResultJson);
			Results.Emplace(TCId, ResultJson, TR.bSuccess);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
		});
}

void FUECPOpenAIAgentLoop::FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results)
{
	AppendAssistantMessage();
	AppendToolResults(Results);

	if (OnBeforeNextRound && !OnBeforeNextRound())
	{
		bPendingSendRound = true;
		return;
	}
	State = EState::Streaming;
	SendRound();
}

void FUECPOpenAIAgentLoop::SkipPausedDispatch()
{
	if (PausedDispatchRemainingIndices.Num() == 0) return;

	TArray<TTuple<FString, FString, bool>> Results = MoveTemp(PausedDispatchPartialResults);
	const FString SkippedJson = TEXT("{\"skipped\":true,\"message\":\"User skipped this tool — continue without it.\"}");
	for (int32 Idx : PausedDispatchRemainingIndices)
	{
		const FToolCallAccum& TC = PendingToolCalls[Idx];
		TSharedPtr<FJsonObject> LA = ParseJson(TC.ArgumentsAccum);
		FString LAction;
		if (LA.IsValid()) LA->TryGetStringField(TEXT("action"), LAction);
		const FString Label = LAction.IsEmpty() ? TC.Name
			: FString::Printf(TEXT("%s \xB7 %s"), *TC.Name, *LAction);
		if (OnToolResult) OnToolResult(TC.Id, Label, false, SkippedJson);
		Results.Emplace(TC.Id, SkippedJson, false);
	}
	PausedDispatchRemainingIndices.Reset();
	PausedDispatchPartialResults.Reset();
	bPendingSendRound = false;

	FinaliseDispatchAndContinue(Results);
}

void FUECPOpenAIAgentLoop::TriggerPendingSendRound()
{
	if (!bPendingSendRound || bStopped.load()) return;
	bPendingSendRound = false;

	if (PausedDispatchRemainingIndices.Num() > 0)
	{
		TArray<int32> Resume = MoveTemp(PausedDispatchRemainingIndices);
		TArray<TTuple<FString, FString, bool>> Results = MoveTemp(PausedDispatchPartialResults);
		PausedDispatchRemainingIndices.Reset();
		PausedDispatchPartialResults.Reset();
		DispatchOneTool(MoveTemp(Resume), 0, MoveTemp(Results));
		return;
	}

	State = EState::Streaming;
	SendRound();
}

void FUECPOpenAIAgentLoop::AppendAssistantMessage()
{
	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("assistant"));

	if (PendingToolCalls.Num() == 0)
	{
		Msg->SetStringField(TEXT("content"), TextAccum);
	}
	else
	{
		if (TextAccum.IsEmpty())
			Msg->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
		else
			Msg->SetStringField(TEXT("content"), TextAccum);

		TArray<int32> Indices;
		PendingToolCalls.GetKeys(Indices);
		Indices.Sort();

		TArray<TSharedPtr<FJsonValue>> ToolCallsArr;
		for (int32 Idx : Indices)
		{
			const FToolCallAccum& TC = PendingToolCalls[Idx];
			TSharedRef<FJsonObject> Func = MakeShared<FJsonObject>();
			Func->SetStringField(TEXT("name"),      TC.Name);
			Func->SetStringField(TEXT("arguments"), TC.ArgumentsAccum);

			TSharedRef<FJsonObject> TCObj = MakeShared<FJsonObject>();
			TCObj->SetStringField(TEXT("id"),   TC.Id);
			TCObj->SetStringField(TEXT("type"), TEXT("function"));
			TCObj->SetObjectField(TEXT("function"), Func);
			ToolCallsArr.Add(MakeShared<FJsonValueObject>(TCObj));
		}
		Msg->SetArrayField(TEXT("tool_calls"), ToolCallsArr);
	}

	Msg->SetStringField(TEXT("reasoning_content"), ReasoningAccum);

	History.Add(MakeShared<FJsonValueObject>(Msg));
}

void FUECPOpenAIAgentLoop::AppendToolResults(const TArray<TTuple<FString, FString, bool>>& Results)
{
	for (const TTuple<FString, FString, bool>& R : Results)
	{
		TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"),         TEXT("tool"));
		Msg->SetStringField(TEXT("tool_call_id"), R.Get<0>());
		Msg->SetStringField(TEXT("content"),      CapToolResultText(R.Get<1>()));
		History.Add(MakeShared<FJsonValueObject>(Msg));
	}
}
