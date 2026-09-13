// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPClaudeAgentLoop.h"
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
#include "Misc/Base64.h"

namespace
{
	FString ToJsonString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	FString ToJsonString(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Arr, W);
		W->Close();
		return Out;
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Str)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
		FJsonSerializer::Deserialize(R, Obj);
		return Obj;
	}

	TSharedPtr<FJsonValue> MakeToolSchema(const FString& Name, const FString& Description)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> ActionProp = MakeShared<FJsonObject>();
		ActionProp->SetStringField(TEXT("type"), TEXT("string"));

		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(TEXT("action"), ActionProp);
		Schema->SetObjectField(TEXT("properties"), Props);
		Schema->SetArrayField(TEXT("required"),
			TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("action")) });
		Schema->SetBoolField(TEXT("additionalProperties"), true);

		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Name);
		Tool->SetStringField(TEXT("description"), Description);
		Tool->SetObjectField(TEXT("input_schema"), Schema);
		return MakeShared<FJsonValueObject>(Tool);
	}

	TSharedPtr<FJsonValue> MakeStandaloneTool(const FString& Name, const FString& Description)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		Schema->SetBoolField(TEXT("additionalProperties"), true);

		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Name);
		Tool->SetStringField(TEXT("description"), Description);
		Tool->SetObjectField(TEXT("input_schema"), Schema);
		return MakeShared<FJsonValueObject>(Tool);
	}
}

TArray<TSharedPtr<FJsonValue>> FUECPClaudeAgentLoop::BuildToolDefinitions()
{
	TArray<TSharedPtr<FJsonValue>> Out;
	auto AddClaudeTool = [&](const FString& Name, const FString& Desc,
		const TSharedRef<FJsonObject>& Props, bool bRequireAction)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Props);
		Schema->SetBoolField(TEXT("additionalProperties"), true);
		if (bRequireAction)
			Schema->SetArrayField(TEXT("required"),
				TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("action")) });
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Name);
		Tool->SetStringField(TEXT("description"), Desc);
		Tool->SetObjectField(TEXT("input_schema"), Schema);
		Out.Add(MakeShared<FJsonValueObject>(Tool));
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
			AddClaudeTool(TEXT("get_tool_docs"), FString(E.Description), Props, false);
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
			AddClaudeTool(TEXT("project_plan"), FString(E.Description), Props, true);
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
			AddClaudeTool(TEXT("memory"), FString(E.Description), MemProps, true);
		}
		else if (E.InputSchema.IsValid())
		{
			// Extension MCP proxy tools carry their server-provided schema; umbrellas carry the
			// metadata-derived one (action enum + typed params) from GetStandardToolEntries.
			TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("name"), E.Name);
			Tool->SetStringField(TEXT("description"), E.Description);
			Tool->SetObjectField(TEXT("input_schema"), E.InputSchema.ToSharedRef());
			Out.Add(MakeShared<FJsonValueObject>(Tool));
		}
		else
		{
			Out.Add(E.bIsUmbrella ? MakeToolSchema(E.Name, E.Description) : MakeStandaloneTool(E.Name, E.Description));
		}
	}
	return Out;
}

TArray<TSharedPtr<FJsonValue>> FUECPClaudeAgentLoop::ConvertHistoryEntryToAnthropic(
	const TSharedPtr<FJsonValue>& GeminiEntry)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	const TSharedPtr<FJsonObject> Obj = GeminiEntry.IsValid() ? GeminiEntry->AsObject() : nullptr;
	if (!Obj.IsValid()) return Out;

	FString Role;
	Obj->TryGetStringField(TEXT("role"), Role);

	if (Role == TEXT("context") || Role == TEXT("tool_bubble")
		|| Role == TEXT("agent_thinking")) return Out;

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!Obj->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->IsEmpty())
		return Out;

	const FString AnthropicRole = (Role == TEXT("model")) ? TEXT("assistant") : TEXT("user");

	TArray<TSharedPtr<FJsonValue>> Content;
	for (const TSharedPtr<FJsonValue>& PartVal : *Parts)
	{
		const TSharedPtr<FJsonObject> Part = PartVal.IsValid() ? PartVal->AsObject() : nullptr;
		if (!Part.IsValid()) continue;

		FString Text;
		if (Part->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
			Block->SetStringField(TEXT("type"), TEXT("text"));
			Block->SetStringField(TEXT("text"), Text);
			Content.Add(MakeShared<FJsonValueObject>(Block));
		}

		const TSharedPtr<FJsonObject>* InlineData = nullptr;
		if (Part->TryGetObjectField(TEXT("inline_data"), InlineData) && InlineData)
		{
			FString MimeType, Data;
			(*InlineData)->TryGetStringField(TEXT("mime_type"), MimeType);
			(*InlineData)->TryGetStringField(TEXT("data"), Data);

			TSharedRef<FJsonObject> Src = MakeShared<FJsonObject>();
			Src->SetStringField(TEXT("type"), TEXT("base64"));
			Src->SetStringField(TEXT("media_type"), MimeType);
			Src->SetStringField(TEXT("data"), Data);

			TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
			Block->SetStringField(TEXT("type"), TEXT("image"));
			Block->SetObjectField(TEXT("source"), Src);
			Content.Add(MakeShared<FJsonValueObject>(Block));
		}
	}

	if (Content.IsEmpty()) return Out;

	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), AnthropicRole);
	Msg->SetArrayField(TEXT("content"), Content);
	Out.Add(MakeShared<FJsonValueObject>(Msg));
	return Out;
}

void FUECPClaudeAgentLoop::Start()
{
	if (bStopped.load()) return;
	check(IsInGameThread());
	State = EState::Streaming;
	Round = 0;
	SendRound();
}

void FUECPClaudeAgentLoop::SendRound()
{
	if (bStopped.load()) return;
	if (Round >= MaxRounds)
	{
		Finish(false, FString::Printf(TEXT("Reached the %d-round limit. Type a follow-up message to continue."), MaxRounds));
		return;
	}
	++Round;
	ResetSseState();

	CurrentBlocks.Empty();
	CurrentStopReason.Empty();
	AccumulatedText.Empty();
	bStreamTransientError = false;
	StreamErrorDetail.Empty();
	FinalInputTokens  = 0;
	FinalOutputTokens = 0;
	{ FScopeLock L(&SseMutex); SseBuffer.Empty(); }

	WindowHistoryForRequest();

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("model"), ModelName);
	Payload->SetNumberField(TEXT("max_tokens"), ResolveMaxOutputTokens());
	{
		TSharedRef<FJsonObject> SysBlock = MakeShared<FJsonObject>();
		SysBlock->SetStringField(TEXT("type"), TEXT("text"));
		SysBlock->SetStringField(TEXT("text"), SystemPrompt);
		TSharedRef<FJsonObject> CacheCtrl = MakeShared<FJsonObject>();
		CacheCtrl->SetStringField(TEXT("type"), TEXT("ephemeral"));
		SysBlock->SetObjectField(TEXT("cache_control"), CacheCtrl);
		Payload->SetArrayField(TEXT("system"),
			TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(SysBlock) });
	}
	Payload->SetBoolField(TEXT("stream"), true);
	Payload->SetArrayField(TEXT("tools"), BuildToolDefinitions());
	Payload->SetArrayField(TEXT("messages"), History);

	FString Body;
	{
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Payload, W);
	}

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetURL(TEXT("https://api.anthropic.com/v1/messages"));
	ActiveRequest->SetHeader(TEXT("Content-Type"),      TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("x-api-key"),         ApiKey);
	ActiveRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	ActiveRequest->SetHeader(TEXT("anthropic-beta"),    TEXT("prompt-caching-2024-07-31"));
	ActiveRequest->SetContentAsString(Body);
	ConfigureRequestTimeout();

	TWeakPtr<FUECPClaudeAgentLoop> WeakSelf = AsShared();
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	ActiveRequest->SetResponseBodyReceiveStreamDelegateV2(
		FHttpRequestStreamDelegateV2::CreateLambda(
			[WeakSelf](void* Ptr, int64& Length)
			{
				TSharedPtr<FUECPClaudeAgentLoop> Self = WeakSelf.Pin();
				if (!Self || !Ptr || Length <= 0) return;
				Self->OnStreamChunk(Ptr, Length);
			}));
#endif

	ActiveRequest->OnRequestProgress64().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, uint64 Sent, uint64 Recv)
		{
			TSharedPtr<FUECPClaudeAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestProgress(Req, Sent, Recv);
		});

	ActiveRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			TSharedPtr<FUECPClaudeAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestComplete(Req, Resp, bOk);
		});

	ActiveRequest->ProcessRequest();
}

void FUECPClaudeAgentLoop::DrainSseBuffer(const FString& NewData)
{
	FString Working = SseRemnant + NewData;
	SseRemnant.Empty();

	int32 EventEnd;
	while ((EventEnd = Working.Find(TEXT("\n\n"))) != INDEX_NONE)
	{
		FString Block = Working.Left(EventEnd);
		Working.MidInline(EventEnd + 2, MAX_int32, EAllowShrinking::No);

		FString EventType;
		FString DataPayload;
		TArray<FString> Lines;
		Block.ParseIntoArray(Lines, TEXT("\n"), true);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("event:")))
				EventType = Line.Mid(6).TrimStart();
			else if (Line.StartsWith(TEXT("data:")))
				DataPayload = Line.Mid(5).TrimStart();
		}

		if (!DataPayload.IsEmpty() && DataPayload != TEXT("[DONE]"))
			ProcessSseEvent(EventType, DataPayload);
	}

	SseRemnant = MoveTemp(Working);
}

void FUECPClaudeAgentLoop::ProcessSseEvent(const FString& EventType, const FString& DataJson)
{
	if (bStopped.load()) return;

	const TSharedPtr<FJsonObject> D = ParseJsonObject(DataJson);
	if (!D.IsValid()) return;

	FString Type;
	D->TryGetStringField(TEXT("type"), Type);

	if (Type == TEXT("message_start"))
	{
		const TSharedPtr<FJsonObject>* MsgPtr = nullptr;
		if (D->TryGetObjectField(TEXT("message"), MsgPtr) && MsgPtr)
		{
			const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
			if ((*MsgPtr)->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr)
			{
				int32 InputTok = 0;
				(*UsagePtr)->TryGetNumberField(TEXT("input_tokens"), InputTok);
				if (InputTok > 0) FinalInputTokens = InputTok;
			}
		}
	}
	else if (Type == TEXT("content_block_start"))
	{
		int32 Idx = 0;
		D->TryGetNumberField(TEXT("index"), Idx);

		FContentBlock& Block = CurrentBlocks.FindOrAdd(Idx);
		Block.Index = Idx;

		const TSharedPtr<FJsonObject>* CB = nullptr;
		if (D->TryGetObjectField(TEXT("content_block"), CB) && CB)
		{
			(*CB)->TryGetStringField(TEXT("type"),    Block.Type);
			(*CB)->TryGetStringField(TEXT("id"),      Block.Id);
			(*CB)->TryGetStringField(TEXT("name"),    Block.ToolName);
		}
	}
	else if (Type == TEXT("content_block_delta"))
	{
		int32 Idx = 0;
		D->TryGetNumberField(TEXT("index"), Idx);
		FContentBlock* Block = CurrentBlocks.Find(Idx);
		if (!Block) return;

		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (!D->TryGetObjectField(TEXT("delta"), Delta) || !Delta) return;

		FString DeltaType;
		(*Delta)->TryGetStringField(TEXT("type"), DeltaType);

		if (DeltaType == TEXT("text_delta"))
		{
			FString Text;
			(*Delta)->TryGetStringField(TEXT("text"), Text);
			Block->TextAccum += Text;
			AccumulatedText += Text;
			if (OnTextDelta) OnTextDelta(Text);
		}
		else if (DeltaType == TEXT("input_json_delta"))
		{
			FString Partial;
			(*Delta)->TryGetStringField(TEXT("partial_json"), Partial);
			Block->InputAccum += Partial;
		}
	}
	else if (Type == TEXT("content_block_stop"))
	{
		int32 Idx = 0;
		D->TryGetNumberField(TEXT("index"), Idx);
		FContentBlock* Block = CurrentBlocks.Find(Idx);
		if (!Block || Block->Type != TEXT("tool_use")) return;

		const FString Preview = Block->InputAccum.Left(300);
		{
			TSharedPtr<FJsonObject> LA = ParseJsonObject(Block->InputAccum);
			FString LAction;
			if (LA.IsValid()) LA->TryGetStringField(TEXT("action"), LAction);
			const FString LLabel = LAction.IsEmpty() ? Block->ToolName
				: FString::Printf(TEXT("%s \xB7 %s"), *Block->ToolName, *LAction);
			if (OnToolStart) OnToolStart(Block->Id, LLabel, Preview);
		}
	}
	else if (Type == TEXT("message_delta"))
	{
		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (D->TryGetObjectField(TEXT("delta"), Delta) && Delta)
			(*Delta)->TryGetStringField(TEXT("stop_reason"), CurrentStopReason);
		const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
		if (D->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr)
		{
			int32 OutputTok = 0;
			(*UsagePtr)->TryGetNumberField(TEXT("output_tokens"), OutputTok);
			if (OutputTok > 0) FinalOutputTokens = OutputTok;
		}
	}
	else if (Type == TEXT("error"))
	{
		FString Detail = FUECPAgentLoopBase::ExtractProviderErrorDetail(DataJson);
		if (Detail.IsEmpty()) Detail = TEXT("(no detail provided by Anthropic)");

		// Mid-stream overloaded / api / rate-limit errors are transient: when nothing has been
		// shown to the user yet, defer to OnRequestComplete so the round can be retried.
		FString ErrType;
		{
			const TSharedPtr<FJsonObject>* ErrObj = nullptr;
			if (D->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
				(*ErrObj)->TryGetStringField(TEXT("type"), ErrType);
		}
		const bool bTransientType =
			ErrType == TEXT("overloaded_error") || ErrType == TEXT("api_error") || ErrType == TEXT("rate_limit_error");
		if (bTransientType && AccumulatedText.IsEmpty() && CurrentBlocks.IsEmpty())
		{
			bStreamTransientError = true;
			StreamErrorDetail = Detail;
			return;
		}

		Finish(false, FString::Printf(TEXT("**Anthropic streaming error**\n\n> %s"), *Detail));
	}
}

void FUECPClaudeAgentLoop::OnRequestComplete(FHttpRequestPtr ,
	FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bStopped.load()) return;

	FString Remaining;
	FString StreamedBody;
	{
		FScopeLock L(&SseMutex);
		Remaining    = MoveTemp(SseBuffer);
		StreamedBody = RawResponseBody;
	}
	if (Remaining.IsEmpty() && StreamedBody.IsEmpty() && Response.IsValid())
	{
		Remaining = RecoverResponseBody(Response, StreamedBody);
		Remaining.ReplaceInline(TEXT("\r"), TEXT(""));
		StreamedBody = Remaining;
	}
	if (!Remaining.IsEmpty()) DrainSseBuffer(Remaining);

	if (OnRoundResponse && Response.IsValid()) OnRoundResponse(Response);

	const bool bRoundHadOutput = !AccumulatedText.IsEmpty() || !CurrentBlocks.IsEmpty();

	if (!bWasSuccessful || !Response.IsValid())
	{
		if (TryScheduleTransientRetry(bWasSuccessful, 0, FString(), Response, TEXT("Anthropic"), bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, 0, TEXT(""), TEXT("Anthropic")));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		const FString Body = RecoverResponseBody(Response, StreamedBody);
		if (TryScheduleTransientRetry(bWasSuccessful, Code, Body, Response, TEXT("Anthropic"), bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, Code, Body, TEXT("Anthropic")));
		return;
	}

	if (bStreamTransientError)
	{
		// HTTP 200 but the stream carried an overloaded/api/rate-limit error event: treat like a 529.
		if (TryScheduleTransientRetry(true, 529, FString(), Response, TEXT("Anthropic"), bRoundHadOutput))
			return;
		Finish(false, FString::Printf(TEXT("**Anthropic streaming error**\n\n> %s"), *StreamErrorDetail));
		return;
	}

	TransientRetryCount = 0;

	if (CurrentStopReason == TEXT("tool_use"))
	{
		State = EState::Dispatching;
		DispatchToolsAndLoop();
	}
	else if (CurrentStopReason == TEXT("max_tokens") && AccumulatedText.IsEmpty() && !CurrentBlocks.IsEmpty())
	{
		Finish(false, TEXT("Response cut off (output token limit reached). Send a follow-up to continue."));
	}
	else
	{
		const bool bHasAnyToolUseBlock = [this]()
		{
			for (const auto& Pair : CurrentBlocks)
				if (Pair.Value.Type == TEXT("tool_use")) return true;
			return false;
		}();
		const bool bLengthCutoff = (CurrentStopReason == TEXT("max_tokens"));
		const bool bIntentStall  = false;
		if (bLengthCutoff || bIntentStall)
		{
			const FString Footer = bLengthCutoff
				? TEXT("\n\n---\n*Response cut off (token limit). Type \"continue\" to resume.*")
				: TEXT("\n\n---\n*Agent ended its turn without firing tools. Type \"continue\" if it should keep going.*");
			AccumulatedText += Footer;
			if (OnTextDelta) OnTextDelta(Footer);
			bStalledNeedsContinue = true;
		}
		AppendAssistantMessage();
		Finish(true);
	}
}

void FUECPClaudeAgentLoop::DispatchToolsAndLoop()
{
	if (bStopped.load()) return;

	AppendAssistantMessage();

	TArray<int32> Indices;
	CurrentBlocks.GetKeys(Indices);
	Indices.Sort();

	for (int32 Idx : Indices)
	{
		if (CurrentBlocks.Contains(Idx) && CurrentBlocks[Idx].Type == TEXT("tool_use"))
			ToolCallsThisLoop++;
	}

	DispatchOneTool(MoveTemp(Indices), 0, TArray<TTuple<FString, FString, bool>>{});
}

void FUECPClaudeAgentLoop::DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
	TArray<TTuple<FString, FString, bool>> Results)
{
	if (bStopped.load()) return;

	while (NextIndex < Indices.Num())
	{
		const int32 Idx = Indices[NextIndex];
		if (CurrentBlocks.Contains(Idx) && CurrentBlocks[Idx].Type == TEXT("tool_use")) break;
		++NextIndex;
	}

	if (NextIndex >= Indices.Num())
	{
		FinaliseDispatchAndContinue(Results);
		return;
	}

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();

	const int32 Idx = Indices[NextIndex];
	FContentBlock& Block = CurrentBlocks[Idx];
	const FString BlockId       = Block.Id;
	const FString BlockToolName = Block.ToolName;
	const FString BlockInput    = Block.InputAccum;

	TSharedPtr<FJsonObject> Args = ParseJsonObject(BlockInput);
	if (!Args.IsValid()) Args = MakeShared<FJsonObject>();

	FString DAction;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), DAction);
	const FString DisplayLabel = DAction.IsEmpty() ? BlockToolName
		: FString::Printf(TEXT("%s \xB7 %s"), *BlockToolName, *DAction);

	auto AccumulateAndAdvance = [this, BlockId, DisplayLabel](
		TArray<int32>& InIndices, int32 InNextIndex,
		TArray<TTuple<FString, FString, bool>>& InResults,
		const FUECPToolResult& Result)
	{
		const FString ResultJson = Result.SummaryJson.IsEmpty()
			? (!Result.ResultJson.IsEmpty() ? Result.ResultJson : Result.ErrorMessage)
			: Result.SummaryJson;
		if (OnToolResult) OnToolResult(BlockId, DisplayLabel, Result.bSuccess, ResultJson);
		InResults.Emplace(BlockId, ResultJson, Result.bSuccess);
		DispatchOneTool(MoveTemp(InIndices), InNextIndex + 1, MoveTemp(InResults));
	};

	FName DispatchName = ResolveDispatchName(BlockToolName, Args);

	{
		FString CrewDeny;
		if (!IUECPCoreModule::Get().GetCrewService().IsToolAllowedForChat(ChatID, DispatchName, CrewDeny))
		{
			FUECPToolResult DR;
			DR.bSuccess     = false;
			DR.ErrorMessage = FString::Printf(TEXT("crew role denies tool: %s"), *CrewDeny);
			AccumulateAndAdvance(Indices, NextIndex, Results, DR);
			return;
		}
	}

	UECPCallerContext::WriteCallerChatId(Args, ChatID);
	{
		FUECPToolResult WResult;
		if (OnWidgetTool && OnWidgetTool(BlockToolName, Args, WResult))
		{
			AccumulateAndAdvance(Indices, NextIndex, Results, WResult);
			return;
		}
	}

	FString DenyReason;
	if (OnBeforeDispatch && !OnBeforeDispatch(BlockToolName, Args, DispatchName, DenyReason))
	{
		if (!DenyReason.IsEmpty())
		{
			FUECPToolResult DR;
			DR.bSuccess = false;
			DR.ResultJson = DenyReason;
			AccumulateAndAdvance(Indices, NextIndex, Results, DR);
			return;
		}

		PausedDispatchRemainingIndices.Reset();
		for (int32 jj = NextIndex; jj < Indices.Num(); ++jj)
			PausedDispatchRemainingIndices.Add(Indices[jj]);
		PausedDispatchPartialResults = MoveTemp(Results);
		bPendingSendRound = true;
		return;
	}

	Dispatcher.ExecuteFromArgsAsync(DispatchName, Args,
		[this, Indices = MoveTemp(Indices), NextIndex, Results = MoveTemp(Results),
		 BlockId, DisplayLabel](FUECPToolResult Result) mutable
		{
			if (bStopped.load()) return;
			const FString ResultJson = Result.SummaryJson.IsEmpty()
				? (!Result.ResultJson.IsEmpty() ? Result.ResultJson : Result.ErrorMessage)
				: Result.SummaryJson;
			if (OnToolResult) OnToolResult(BlockId, DisplayLabel, Result.bSuccess, ResultJson);
			Results.Emplace(BlockId, ResultJson, Result.bSuccess);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
		});
}

void FUECPClaudeAgentLoop::FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results)
{
	AppendToolResults(Results);

	if (OnBeforeNextRound && !OnBeforeNextRound())
	{
		bPendingSendRound = true;
		return;
	}
	State = EState::Streaming;
	SendRound();
}

void FUECPClaudeAgentLoop::SkipPausedDispatch()
{
	if (PausedDispatchRemainingIndices.Num() == 0) return;

	TArray<TTuple<FString, FString, bool>> Results = MoveTemp(PausedDispatchPartialResults);
	const FString SkippedJson = TEXT("{\"skipped\":true,\"message\":\"User skipped this tool — continue without it.\"}");
	for (int32 Idx : PausedDispatchRemainingIndices)
	{
		if (!CurrentBlocks.Contains(Idx)) continue;
		FContentBlock& Block = CurrentBlocks[Idx];
		if (Block.Type != TEXT("tool_use")) continue;
		TSharedPtr<FJsonObject> Args = ParseJsonObject(Block.InputAccum);
		FString DAction;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), DAction);
		const FString DisplayLabel = DAction.IsEmpty() ? Block.ToolName
			: FString::Printf(TEXT("%s \xB7 %s"), *Block.ToolName, *DAction);
		if (OnToolResult) OnToolResult(Block.Id, DisplayLabel, false, SkippedJson);
		Results.Emplace(Block.Id, SkippedJson, false);
	}
	PausedDispatchRemainingIndices.Reset();
	PausedDispatchPartialResults.Reset();
	bPendingSendRound = false;

	FinaliseDispatchAndContinue(Results);
}

void FUECPClaudeAgentLoop::TriggerPendingSendRound()
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

void FUECPClaudeAgentLoop::AppendAssistantMessage()
{
	TArray<TSharedPtr<FJsonValue>> Content;

	if (!AccumulatedText.IsEmpty())
	{
		TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), AccumulatedText);
		Content.Add(MakeShared<FJsonValueObject>(TextBlock));
	}

	TArray<int32> Indices;
	CurrentBlocks.GetKeys(Indices);
	Indices.Sort();

	for (int32 Idx : Indices)
	{
		const FContentBlock& Block = CurrentBlocks[Idx];
		if (Block.Type != TEXT("tool_use")) continue;

		TSharedPtr<FJsonObject> Input = ParseJsonObject(Block.InputAccum);
		if (!Input.IsValid()) Input = MakeShared<FJsonObject>();

		TSharedRef<FJsonObject> ToolBlock = MakeShared<FJsonObject>();
		ToolBlock->SetStringField(TEXT("type"), TEXT("tool_use"));
		ToolBlock->SetStringField(TEXT("id"),   Block.Id);
		ToolBlock->SetStringField(TEXT("name"), Block.ToolName);
		ToolBlock->SetObjectField(TEXT("input"), Input);
		Content.Add(MakeShared<FJsonValueObject>(ToolBlock));
	}

	if (Content.IsEmpty()) return;

	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("assistant"));
	Msg->SetArrayField(TEXT("content"), Content);
	History.Add(MakeShared<FJsonValueObject>(Msg));
}

void FUECPClaudeAgentLoop::AppendToolResults(
	const TArray<TTuple<FString, FString, bool>>& Results)
{
	if (Results.IsEmpty()) return;

	TArray<TSharedPtr<FJsonValue>> Content;
	for (const auto& [ToolUseId, ResultJson, bSuccess] : Results)
	{
		TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"),        TEXT("tool_result"));
		Block->SetStringField(TEXT("tool_use_id"), ToolUseId);
		Block->SetStringField(TEXT("content"),     ResultJson.IsEmpty()
			? FString(bSuccess ? TEXT("{}") : TEXT("{\"error\":\"No result\"}"))
			: CapToolResultText(ResultJson));
		Block->SetBoolField(TEXT("is_error"), !bSuccess);
		Content.Add(MakeShared<FJsonValueObject>(Block));
	}

	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("user"));
	Msg->SetArrayField(TEXT("content"), Content);
	History.Add(MakeShared<FJsonValueObject>(Msg));
}
